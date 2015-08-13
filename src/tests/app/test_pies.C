
/*
 * Author
 *   David Blom, TU Delft. All rights reserved.
 */

#include "PIES.H"
#include "Piston.H"
#include "gtest/gtest.h"

TEST( PIESTest, coefficients )
{
    scalar dt, q0, qdot0, As, Ac, omega, endTime, tol, delta, rho;

    int nbTimeSteps = 100, nbNodes;

    endTime = 100;
    dt = endTime / nbTimeSteps;
    As = 100;
    Ac = As;
    omega = 1;
    q0 = -As;
    qdot0 = -As;
    tol = 1.0e-10;
    delta = 1.0e-9;
    nbNodes = 10;
    rho = 3.15;

    std::shared_ptr<sdc::AdaptiveTimeStepper> adaptiveTimeStepper;
    std::shared_ptr<Piston> piston;

    adaptiveTimeStepper = std::shared_ptr<sdc::AdaptiveTimeStepper> ( new sdc::AdaptiveTimeStepper( false ) );

    piston = std::shared_ptr<Piston> ( new Piston( nbTimeSteps, dt, q0, qdot0, As, Ac, omega ) );

    std::shared_ptr<sdc::PIES> pies( new sdc::PIES( piston, adaptiveTimeStepper, rho, delta, tol, nbNodes, 10 * nbNodes ) );

    pies->run();

    fsi::vector solution( 2 );
    piston->getSolution( solution );

    scalar ref = piston->referenceSolution( 100 );
    scalar error = std::abs( solution( 1 ) - ref ) / std::abs( ref );
    Info << "error = " << error << endl;
    ASSERT_LT( error, 1.0e-10 );
}