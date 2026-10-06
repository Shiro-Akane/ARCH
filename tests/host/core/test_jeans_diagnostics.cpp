/**
 * @file test_jeans_diagnostics.cpp
 * @brief CPU numeric qualification of the isolated Jeans formula.
 * Independent Decimal 80/120-digit references use published pi and CGS G.
 * The 16-double-epsilon check is an engineering rounding check, not an EOS,
 * AMR threshold, trajectory or scientific acceptance budget.
 */
#include "physics/diagnostics/JeansDiagnostics.h"
#include "physics/eos/IdealGas.h"
#include "amr/transfer/RegridTransferMath.h"
#include "amr/refinement/RefinementThermodynamics.h"
#include "../../math/physics/JeansNumericCases.h"
#include <array>
#include <limits>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

namespace {
/** Compare real conservative restriction + EOS against independent caloric integrals.
 * This is a bounded static parent-state gate, not lifecycle or RZ swirl acceptance.
 */
void tree_transaction_contract()
{
    SimConfig config;
    config.grid.dim=1;config.grid.nblockx1=1;
    config.grid.nblockx2=0;config.grid.nblockx3=0;
    config.grid.x1_max=8.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=2;
    config.amr.refine_on_rho=false;
    config.amr.refine_on_jeans=true;config.amr.jeans_cells=8.;
    SpeciesManager species;IdealGas eos(1.5,species);
    auto pool=std::make_shared<amr::MemoryPool>(16,1);
    amr::AmrTree tree(pool);
    tree.InitRootGrid(config,0);
    amr::BindRefinementThermodynamics(tree,eos);
    auto fill=[&](amr::AmrTree& target,amr::MemoryPool& memory,double energy) {
        for(int id:target.GetActiveBlocks()) {
            auto& block=memory.GetBlock(id);
            for(int cell=0;cell<block.grid.GetTotalSize();++cell)
                block.fluid_state.set(cell,FluidVector{1e7,0.,0.,0.,1e7*energy});
        }
    };
    auto require=[](bool ok,const char* message) {
        if(!ok)throw std::runtime_error(message);
    };
    fill(tree,*pool,4./3.);
    const double root=tree.MinimumJeansCells(pool->GetBlock(tree.GetActiveBlocks().front()));
    require(root>=4.&&root<8.,"fixture root resolution changed");
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==2,"JENS failed real refinement");
    require(!tree.Regrid(config)&&tree.GetActiveBlocks().size()==2,"underresolved parent was coarsened");
    config.amr.jeans_cells=16.;
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==4,"JENS second level failed");
    const auto before=tree.GetActiveBlocks();
    const int allocated=pool->GetNumActiveBlocks();
    config.amr.jeans_cells=32.;
    bool rejected=false;
    try {tree.Regrid(config);}catch(const std::runtime_error&){rejected=true;}
    require(rejected&&tree.GetActiveBlocks()==before&&pool->GetNumActiveBlocks()==allocated,
            "lrefinemax failure published partial topology");
    config.amr.jeans_cells=8.;fill(tree,*pool,12.);
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==2,"resolved parent merge failed");
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==1,"resolved root merge failed");
    fill(tree,*pool,4./3.);
    config.amr.jeans_cells=root;
    require(!tree.Regrid(config),"threshold equality refined");
    config.amr.jeans_cells=std::nextafter(root,std::numeric_limits<double>::infinity());
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==2,"one-ULP underresolution did not refine");
    require(!tree.Regrid(config),"one-ULP underresolved parent merged");
    config.amr.jeans_cells=root;
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==1,"parent equality did not permit merge");
    // The accepted-macro repair mode must neither coarsen nor run normal
    // curvature indicators between their configured regrid boundaries.
    config.amr.jeans_cells=8.;
    {
        auto prepared=tree.PrepareRegrid(config,{}, {}, {},true);
        require(prepared.topology_changed(),"repair mode missed unresolved root");
        prepared.AbortNoexcept();
    }
    fill(tree,*pool,12.);
    {
        auto prepared=tree.PrepareRegrid(config,{}, {}, {},true);
        require(!prepared.topology_changed(),"repair mode changed resolved topology");
        prepared.PublishNoChangeNoexcept();
    }
    int calls=0;
    tree.SetJeansEvaluator([&](const FluidVector&,const double*,
        const GridMetrics::GeometryView&,int,int)->JeansDiagnostics::Resolution {
        ++calls;throw std::runtime_error("disabled JENS invoked EOS");
    });
    config.amr.refine_on_jeans=false;
    tree.EvaluateRefinement(config);
    require(calls==0,"disabled JENS traversed EOS");
    amr::BindRefinementThermodynamics(tree,eos);
    config.amr.refine_on_jeans=true;
    for(double bad:{0.,3.,std::numeric_limits<double>::quiet_NaN()}) {
        config.amr.jeans_cells=bad;rejected=false;
        try {tree.Regrid(config);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected&&tree.GetActiveBlocks().size()==1,"invalid target accepted");
    }
    auto small_pool=std::make_shared<amr::MemoryPool>(2,1);
    amr::AmrTree small(small_pool);
    small.InitRootGrid(config,0);fill(small,*small_pool,4./3.);
    amr::BindRefinementThermodynamics(small,eos);
    config.amr.jeans_cells=8.;const auto small_before=small.GetActiveBlocks();
    rejected=false;
    try {small.Regrid(config);}catch(const std::runtime_error&){rejected=true;}
    require(rejected&&small.GetActiveBlocks()==small_before&&small_pool->GetNumActiveBlocks()==1,
            "capacity failure leaked blocks or published partial topology");
    std::cout<<"JEANS_TREE_TRANSACTION_PASS\n";
}

void frozen_uniform_tree_gate()
{
    SimConfig config;config.grid.dim=1;config.grid.nblockx1=4;
    config.grid.nblockx2=0;config.grid.nblockx3=0;config.grid.x1_max=1.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.amr.refine_on_rho=false;config.amr.refine_on_jeans=true;
    config.amr.jeans_cells=160.;
    SpeciesManager species;species.add_species("gas",1.,1.,1.6666666666666667,1.);
    IdealGas eos(1.6666666666666667,species);
    auto initialize=[&](amr::AmrTree& tree,amr::MemoryPool& pool) {
        tree.InitRootGrid(config,1);
        for(int id:tree.GetActiveBlocks()) {
            auto& block=pool.GetBlock(id);
            for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
                block.fluid_state.set(cell,{1e7,0.,0.,0.,1e7});
                block.fluid_state.X(0,cell)=1.;
            }
        }
        amr::BindRefinementThermodynamics(tree,eos);
    };
    auto require=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    auto pool=std::make_shared<amr::MemoryPool>(12,1);
    amr::AmrTree tree(pool);initialize(tree,*pool);
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==8,
            "frozen uniform160 did not refine 4 roots into 8 leaves");
    require(!tree.Regrid(config),"frozen160 allowed unresolved parent");
    config.amr.jeans_cells=64.;
    require(tree.Regrid(config)&&tree.GetActiveBlocks().size()==4,
            "frozen64 refused resolved restricted parent");
    config.amr.jeans_cells=160.;
    auto short_pool=std::make_shared<amr::MemoryPool>(11,1);
    amr::AmrTree short_tree(short_pool);initialize(short_tree,*short_pool);
    const auto before=short_tree.GetActiveBlocks();bool rejected=false;
    try {short_tree.Regrid(config);}catch(const std::runtime_error&){rejected=true;}
    require(rejected&&short_tree.GetActiveBlocks()==before&&short_pool->GetNumActiveBlocks()==4,
            "frozen staged capacity failure published/leaked blocks");
    std::cout<<"JEANS_FROZEN_UNIFORM_TREE_PASS\n";
}

void parent_state_reference()
{
    constexpr long double pi=3.141592653589793238462643383279502884L;
    constexpr long double G=6.67430e-8L;
    const double u=std::numeric_limits<double>::epsilon()/2.;
    // Four positive-volume products/sums, measure conversion and state
    // recovery: gamma_12 propagates the bounded arithmetic stage. The existing
    // well-conditioned IdealGas/Jeans closure retains its 16 epsilon bound.
    const double bound=12*u/(1-12*u)+16*std::numeric_limits<double>::epsilon();
    SpeciesManager empty;
    IdealGas simple(1.5,empty);
    SpeciesManager species;
    species.add_species("first",1.,1.,1.5,2.);
    species.add_species("second",2.,1.,2.,4.);
    IdealGas mixture(1.4,species);
    double maximum=0.;
    int cases=0;
    for(int chart=0;chart<3;++chart) for(bool mixed:{false,true}) {
        const bool rz=chart!=0;
        const double radius=chart==2?2.:0.;
        auto grid=GridMetrics::make_geometry_view(
            rz?GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Cartesian,
            2,{radius,0.,0.},{1.,1.,1.});
        if(rz)grid=GridMetrics::make_rz_geometry_view(grid);
        auto coarse=grid;coarse.dx1=2.;coarse.dx2=2.;
        std::array<std::array<double,4>,6> fields{};
        std::array<double,8> fractions{};
        amr::regrid_math::ConstStateView source{};
        for(int f=0;f<6;++f)source.fields[f]=fields[f].data();
        source.fractions=fractions.data();source.species_stride=4;
        amr::regrid_math::RestrictionGeometry geometry{};
        geometry.count=4;geometry.coarse_volume=GridMetrics::CellVolume(coarse,0,0,0);
        geometry.angular_momentum=rz;
        if(rz)geometry.coarse_angular_measure=GridMetrics::Rz::AngularMomentumMeasure(
            radius,radius+2.,2.);
        std::array<long double,5> integral{};
        std::array<long double,2> species_integral{};
        long double volume=0., internal_integral=0., angular_measure=0., angular_integral=0.;
        for(int cell=0;cell<4;++cell) {
            const int i=cell%2,j=cell/2;
            const double density=i?2.:1., velocity=(j?-1.:1.)*(i?1.:2.);
            const double internal=i?8.:4.;
            fields[0][cell]=density;fields[1][cell]=density*velocity;
            const double swirl=rz?(i?-1.:2.):0.;
            fields[3][cell]=density*swirl;
            fields[4][cell]=density*(internal+.5*(velocity*velocity+swirl*swirl));
            fractions[cell]=i?.25:.75;fractions[4+cell]=1.-fractions[cell];
            geometry.source_cells[cell]=cell;
            geometry.volumes[cell]=GridMetrics::CellVolume(grid,i,j,0);
            const long double left=radius+i,right=left+1.;
            const long double measure=rz?pi*(right*right-left*left):1.L;
            if(rz) {
                geometry.angular_measures[cell]=GridMetrics::Rz::AngularMomentumMeasure(
                    static_cast<double>(left),static_cast<double>(right),1.);
                // Independent integral 2*pi*int r^2 dr, not the production helper.
                const long double w=(2.L/3.L)*pi*(right*right*right-left*left*left);
                angular_measure+=w;angular_integral+=w*fields[3][cell];
            }
            volume+=measure;internal_integral+=measure*density*internal;
            for(int f=0;f<5;++f)integral[f]+=measure*fields[f][cell];
            for(int sp=0;sp<2;++sp)
                species_integral[sp]+=measure*density*fractions[4*sp+cell];
        }
        double workspace[2]{};
        amr::regrid_math::RestrictionResult parent{};
        if(amr::regrid_math::restrict_family(source,geometry,mixed?2:0,
                0.,0.,workspace,parent)!=amr::regrid_math::Status::Ok)
            throw std::runtime_error("Jeans parent restriction rejected bounded fixture");
        const long double rho=integral[0]/volume;
        const long double azimuth_m=rz?angular_integral/angular_measure:integral[3]/volume;
        const long double kinetic=(integral[1]*integral[1]+integral[2]*integral[2])
            /(2*integral[0]*integral[0])+.5L*(azimuth_m/rho)*(azimuth_m/rho);
        if(rz && std::abs(parent.fluid.mom_w-azimuth_m)>bound*std::max(1.L,std::abs(azimuth_m)))
            throw std::runtime_error("Jeans RZ parent bypassed W angular restriction");
        const long double internal=integral[4]/integral[0]-kinetic;
        if(!(internal>internal_integral/integral[0]))
            throw std::runtime_error("opposite-velocity parent lost unresolved kinetic energy");
        long double gamma=1.5L;
        if(mixed) {
            const long double x=species_integral[0]/integral[0];
            gamma=1+(x*2*.5L+(1-x)*4)/(x*2+(1-x)*4);
            for(int sp=0;sp<2;++sp)
                if(std::abs(parent.fractions[sp]-species_integral[sp]/integral[0])>bound)
                    throw std::runtime_error("Jeans parent composition is not rho-X weighted");
        }
        const auto& eos=mixed?mixture:simple;
        const double* composition=mixed?parent.fractions:nullptr;
        const double pressure=eos.get_pressure(parent.fluid,composition);
        const double sound=eos.get_sound_speed(parent.fluid,pressure,composition);
        const auto actual=JeansDiagnostics::evaluate_cell(parent.fluid.rho,sound*sound,coarse,0,0);
        const long double expected=std::sqrt(pi*gamma*(gamma-1)*internal/(G*rho))/2;
        const double error=static_cast<double>(std::abs(actual.cells-expected)/expected);
        maximum=std::max(maximum,error);
        if(actual.status!=JeansDiagnostics::Status::valid || error>bound) {
            std::cerr<<std::setprecision(20)<<"JEANS_PARENT_FAILURE chart="<<chart
                <<" mixed="<<mixed<<" error="<<error<<" bound="<<bound
                <<" actual="<<actual.cells<<" expected="<<expected
                <<" rho="<<parent.fluid.rho<<" mphi="<<parent.fluid.mom_w
                <<" E="<<parent.fluid.eng<<" internal_ref="<<internal
                <<" pressure="<<pressure<<" sound="<<sound<<'\n';
            throw std::runtime_error("Jeans independent restricted-parent reference mismatch");
        }
        ++cases;
    }
    std::cout<<std::setprecision(17)<<"JEANS_PARENT_CASES="<<cases
             <<" MAX_RELATIVE_ERROR="<<maximum<<" PROPAGATED_ENGINEERING_BOUND="<<bound<<'\n';
}
}

int main()
{
    struct Case { double rho, cs2, h, expected; };
    const std::array<Case, 8> cases{{
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+11},
        {0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+13},
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.accc1f12f6cbdp+10},
        {0x0.00000000007e8p-1022, 0x1.56e1fc2f8f359p-997, 0x1.5af1d78b58c40p+66, 0x1.70560f248ababp-21},
        {0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.56e1fc2f8f359p-997, 0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.accc1f12f6cbcp+12},
        {0x1.7e43c8800759cp+996, 0x1.56e1fc2f8f359p-997, 0x1.56e1fc2f8f359p-997, 0x1.accc1f12f6cbcp+12}
    }};
    for (const auto& c : cases) {
        const auto result = JeansDiagnostics::evaluate(c.rho, c.cs2, c.h);
        if (result.status != JeansDiagnostics::Status::valid ||
            std::abs(result.cells - c.expected) >
                16 * std::numeric_limits<double>::epsilon() * c.expected)
            throw std::runtime_error("Jeans numeric reference mismatch");
    }
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        for (const auto& args : {std::array<double,3>{invalid,1,1},
                                std::array<double,3>{1,invalid,1},
                                std::array<double,3>{1,1,invalid}})
            if (JeansDiagnostics::evaluate(args[0],args[1],args[2]).status !=
                JeansDiagnostics::Status::invalid_input)
                throw std::runtime_error("Invalid Jeans numeric input accepted");
    }
    const auto overflow = JeansDiagnostics::evaluate(1e-300,1e300,1e-300);
    const auto underflow = JeansDiagnostics::evaluate(1e300,1e-300,1e300);
    if (overflow.status != JeansDiagnostics::Status::unrepresentable ||
        underflow.status != JeansDiagnostics::Status::unrepresentable)
        throw std::runtime_error("Jeans nonrepresentable result hidden");
    double exponent_max_relative_error = 0.0;
    int exponent_valid = 0, exponent_unrepresentable = 0;
    for (const auto& c : JeansNumericReference::cases) {
        const auto result = JeansDiagnostics::evaluate(c.rho, c.cs2, c.h);
        if (!std::isfinite(c.expected) || c.expected == 0.0) {
            if (result.status != JeansDiagnostics::Status::unrepresentable ||
                result.cells != c.expected)
                throw std::runtime_error("Jeans exponent-grid range failure hidden");
            ++exponent_unrepresentable;
            continue;
        }
        // Long-double difference avoids underflow in the engineering error
        // calculation itself. Retain the existing 16-double-epsilon criterion.
        const double relative_error = static_cast<double>(
            std::abs(static_cast<long double>(result.cells)-c.expected)/c.expected);
        exponent_max_relative_error = std::max(exponent_max_relative_error, relative_error);
        if (result.status != JeansDiagnostics::Status::valid ||
            relative_error > 16 * std::numeric_limits<double>::epsilon())
            throw std::runtime_error("Jeans exponent-grid reference mismatch");
        ++exponent_valid;
    }
    std::cout << std::setprecision(17)
              << "JEANS_EXPONENT_VALID=" << exponent_valid
              << " UNREPRESENTABLE=" << exponent_unrepresentable
              << " MAX_RELATIVE_ERROR=" << exponent_max_relative_error << '\n';
    // Independent spacing expectations for the maintained native coordinates.
    // Do not use production PhysicalSpacing to construct the expected result.
    GridMetrics::GeometryView grid{};
    grid.dx1 = 1.0; grid.dx2 = 3.0; grid.dx3 = 4.0; grid.x1_min = 1.5;
    const auto check_spacing = [&](double expected_spacing) {
        const auto actual = JeansDiagnostics::evaluate_cell(1, 1, grid, 0, 0);
        const double expected = cases[0].expected / expected_spacing;
        if (actual.status != JeansDiagnostics::Status::valid ||
            std::abs(actual.cells - expected) >
                16 * std::numeric_limits<double>::epsilon() * expected)
            throw std::runtime_error("Jeans physical spacing mismatch");
    };
    grid.dim = 1; check_spacing(1.0);
    // Inactive axes may contain unusable values and must never be evaluated.
    grid.dx2 = std::numeric_limits<double>::quiet_NaN();
    grid.dx3 = -1.0; check_spacing(1.0);
    grid.dx2 = 3.0; grid.dim = 2; check_spacing(3.0);
    grid.dx3 = 4.0; grid.dim = 3; check_spacing(4.0);
    grid.geometry = GridMetrics::Geometry::Cylindrical;
    grid.dim = 2; check_spacing(6.0); // r=2, polar arc r*dphi.
    grid.dim = 3; check_spacing(8.0); // (r,z,phi), largest r*dphi.
    grid.geometry = GridMetrics::Geometry::Spherical;
    grid.dim = 2; check_spacing(6.0); // Existing 2-D polar specialization.
    grid.dim = 3; grid.dx2 = .25;
    grid.x2_min = arch::constants::math::pi / 2 - .125;
    check_spacing(8.0);
    grid.x2_min = 0.0; grid.dx2 = .01;
    check_spacing(1.0); // Near polar axis: radial spacing wins, no floor.
    grid.x1_min = 0.0; grid.dx1 = .25; grid.dx2 = .1; grid.dx3 = .1;
    check_spacing(.25); // First radial cell: no origin/radius repair.
    for (int dimension : {0, 4}) {
        grid.dim = dimension;
        if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
                JeansDiagnostics::Status::invalid_input)
            throw std::runtime_error("Invalid Jeans dimension accepted");
    }
    grid.dim = 2; grid.geometry = GridMetrics::Geometry::Cartesian;
    for (double spacing : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        grid.dx2 = spacing;
        if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
                JeansDiagnostics::Status::invalid_input)
            throw std::runtime_error("Invalid active spacing hidden by max");
    }
    grid.geometry = GridMetrics::Geometry::Unsupported; grid.dim = 1;
    if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
            JeansDiagnostics::Status::invalid_input)
        throw std::runtime_error("Unknown Jeans geometry accepted");
    // Independent caloric IdealGas reference, including moving fluid and a
    // two-species mixture whose gamma differs from the constructor fallback.
    // No production EOS function contributes to the expected expression.
    SpeciesManager empty_species;
    IdealGas simple(1.5, empty_species);
    SpeciesManager mixture_species;
    mixture_species.add_species("first", 1.0, 1.0, 1.5, 2.0);
    mixture_species.add_species("second", 2.0, 1.0, 2.0, 4.0);
    IdealGas mixture(1.4, mixture_species);
    const std::array<double,2> composition{.25,.75};
    const long double reference_pi =
        3.141592653589793238462643383279502884L;
    const long double reference_G = 6.67430e-8L;
    double max_relative_error = 0.0;
    int eos_cases = 0;
    for (bool use_mixture : {false,true})
    for (double density : {.25,1.0,4.0})
    for (double specific_internal_energy : {4.0,16.0})
    for (bool moving : {false,true}) {
        const auto& eos = use_mixture ? mixture : simple;
        const double* Xi = use_mixture ? composition.data() : nullptr;
        const double u = moving ? 2.0 : 0.0;
        const double v = moving ? 3.0 : 0.0;
        const double w = moving ? 4.0 : 0.0;
        const FluidVector state{density,density*u,density*v,density*w,
            density*(specific_internal_energy + .5*(u*u+v*v+w*w))};
        const double pressure = eos.get_pressure(state,Xi);
        const double sound_speed = eos.get_sound_speed(state,pressure,Xi);
        GridMetrics::GeometryView physical_grid{};
        physical_grid.dim = 2; physical_grid.dx1 = .25; physical_grid.dx2 = .5;
        const auto actual = JeansDiagnostics::evaluate_cell(
            state.rho,sound_speed*sound_speed,physical_grid,0,0);
        // Cv-weighted gamma = 1 + (1/4*2*1/2 + 3/4*4*1)/(1/4*2 + 3/4*4).
        const long double gamma = use_mixture ? 27.0L/14.0L : 1.5L;
        const long double expected = std::sqrt(reference_pi *
            gamma*(gamma-1)*specific_internal_energy/(reference_G*density))/.5L;
        const double relative_error = static_cast<double>(
            std::abs(static_cast<long double>(actual.cells)-expected)/expected);
        max_relative_error = std::max(max_relative_error,relative_error);
        if (actual.status != JeansDiagnostics::Status::valid ||
            relative_error > 16*std::numeric_limits<double>::epsilon())
            throw std::runtime_error("Jeans IdealGas independent reference mismatch");
        ++eos_cases;
    }
    std::cout << std::setprecision(17) << "JEANS_IDEALGAS_CASES=" << eos_cases
              << " MAX_RELATIVE_ERROR=" << max_relative_error << '\n';
    parent_state_reference();
    tree_transaction_contract();
    frozen_uniform_tree_gate();
    std::cout << "JEANS_DIAGNOSTICS_NUMERIC_PASS\n";
}
