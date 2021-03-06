
/*
 * Author
 *   David Blom, TU Delft. All rights reserved.
 */

#include <memory>
#include <yaml-cpp/yaml.h>

#include "SolidSolver.H"
#include "SDCSolidSolver.H"
#include "TimeIntegrationScheme.H"
#include "AdaptiveTimeStepper.H"
#include "SDCSolver.H"
#include "ESDIRK.H"
#include "SDC.H"
#include "PIES.H"

int main(
    int argc,
    char * argv[]
    )
{
    std::shared_ptr<argList> args( new argList( argc, argv ) );

    if ( !args->checkRootCase() )
    {
        FatalError.exit();
    }

    std::shared_ptr<Time> runTime( new Time
        (
            Time::controlDictName,
            args->rootPath(),
            args->caseName()
        ) );

    string filename = static_cast<std::string>( args->rootPath() ) + "/" + static_cast<std::string>( args->globalCaseName() ) + "/constant/fsi.yaml";

    YAML::Node config = YAML::LoadFile( filename );

    assert( config["time-integration-scheme"] );
    assert( config["adaptive-time-stepping"] );

    std::string timeIntegrationScheme = config["time-integration-scheme"].as<std::string>();

    assert( timeIntegrationScheme == "bdf" || timeIntegrationScheme == "esdirk" || timeIntegrationScheme == "sdc" || timeIntegrationScheme == "picard-integral-exponential-solver" );

    std::shared_ptr<foamSolidSolver> solid;
    std::shared_ptr<sdc::TimeIntegrationScheme> timeSolver;
    std::shared_ptr<sdc::AdaptiveTimeStepper> adaptiveTimeStepper;

    YAML::Node adaptiveTimeConfig( config["adaptive-time-stepping"] );
    assert( adaptiveTimeConfig["enabled"] );
    bool adaptiveTimeStepping = adaptiveTimeConfig["enabled"].as<bool>();

    std::string filter = "elementary";
    scalar adaptiveTolerance = 1.0e-3;
    scalar safetyFactor = 0.5;

    if ( adaptiveTimeStepping )
    {
        assert( adaptiveTimeConfig["filter"] );
        assert( adaptiveTimeConfig["tolerance"] );
        assert( adaptiveTimeConfig["safety-factor"] );
        filter = adaptiveTimeConfig["filter"].as<std::string>();
        adaptiveTolerance = adaptiveTimeConfig["tolerance"].as<scalar>();
        safetyFactor = adaptiveTimeConfig["safety-factor"].as<scalar>();
    }

    adaptiveTimeStepper = std::shared_ptr<sdc::AdaptiveTimeStepper> ( new sdc::AdaptiveTimeStepper( adaptiveTimeStepping, filter, adaptiveTolerance, safetyFactor ) );

    if ( timeIntegrationScheme == "bdf" )
    {
        assert( not adaptiveTimeStepping );
        solid = std::shared_ptr<foamSolidSolver>( new SolidSolver( fvMesh::defaultRegion, args, runTime ) );
    }

    std::shared_ptr<sdc::SDCSolver> solver;

    if ( timeIntegrationScheme == "esdirk" || timeIntegrationScheme == "sdc" || timeIntegrationScheme == "picard-integral-exponential-solver" )
        solver = std::shared_ptr<sdc::SDCSolver>( new SDCSolidSolver( Foam::fvMesh::defaultRegion, args, runTime ) );

    if ( timeIntegrationScheme == "esdirk" )
    {
        YAML::Node esdirkConfig( config["esdirk"] );

        assert( esdirkConfig["method"] );
        assert( adaptiveTimeStepper );
        assert( solver );

        std::string method = esdirkConfig["method"].as<std::string>();

        timeSolver = std::shared_ptr<sdc::TimeIntegrationScheme>( new sdc::ESDIRK( solver, method, adaptiveTimeStepper ) );
    }

    if ( timeIntegrationScheme == "sdc" )
    {
        assert( config["sdc"] );
        YAML::Node sdcConfig( config["sdc"] );
        assert( sdcConfig["convergence-tolerance"] );
        assert( sdcConfig["number-of-points"] );
        assert( sdcConfig["quadrature-rule"] );
        assert( sdcConfig["min-sweeps"] );
        assert( sdcConfig["max-sweeps"] );
        assert( adaptiveTimeStepper );
        assert( solver );

        int n = sdcConfig["number-of-points"].as<int>();
        scalar tol = sdcConfig["convergence-tolerance"].as<scalar>();
        std::string quadratureRule = sdcConfig["quadrature-rule"].as<std::string>();
        int minSweeps = sdcConfig["min-sweeps"].as<int>();
        int maxSweeps = sdcConfig["max-sweeps"].as<int>();

        timeSolver = std::shared_ptr<sdc::TimeIntegrationScheme> ( new sdc::SDC( solver, adaptiveTimeStepper, quadratureRule, n, tol, minSweeps, maxSweeps ) );
    }

    if ( timeIntegrationScheme == "picard-integral-exponential-solver" )
    {
        assert( config["picard-integral-exponential-solver"] );
        YAML::Node piesConfig( config["picard-integral-exponential-solver"] );
        assert( piesConfig["delta"] );
        assert( piesConfig["convergence-tolerance"] );
        assert( piesConfig["min-sweeps"] );
        assert( piesConfig["max-sweeps"] );
        assert( piesConfig["rho"] );
        assert( solver );

        scalar delta = piesConfig["delta"].as<scalar>();
        scalar tol = piesConfig["convergence-tolerance"].as<scalar>();
        scalar rho = piesConfig["rho"].as<scalar>();
        int minSweeps = piesConfig["min-sweeps"].as<int>();
        int maxSweeps = piesConfig["max-sweeps"].as<int>();

        timeSolver = std::shared_ptr<sdc::TimeIntegrationScheme> ( new sdc::PIES( solver, adaptiveTimeStepper, rho, delta, tol, minSweeps, maxSweeps ) );
    }

    assert( solid || timeSolver );

    if ( solid )
        solid->run();

    if ( timeSolver )
        timeSolver->run();

    Info << "End\n" << endl;

    return 0;
}

// ************************************************************************* //
