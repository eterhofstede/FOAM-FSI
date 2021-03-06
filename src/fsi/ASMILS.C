
/*
 * Author
 *   David Blom, TU Delft. All rights reserved.
 */

#include "ASMILS.H"

using namespace fsi;

ASMILS::ASMILS(
    shared_ptr<SurrogateModel> fineModel,
    shared_ptr<SurrogateModel> coarseModel,
    int maxIter,
    int maxUsedIterations,
    int nbReuse,
    int reuseInformationStartingFromTimeIndex,
    scalar singularityLimit,
    scalar beta
    )
    :
    SpaceMapping( fineModel, coarseModel, maxIter, maxUsedIterations, nbReuse, reuseInformationStartingFromTimeIndex, singularityLimit ),
    beta( beta )
{
    assert( beta > 0 );
    assert( beta <= 1 );
}

ASMILS::~ASMILS()
{}

void ASMILS::fixedUnderRelaxation(
    vector & xk,
    vector & zstar,
    vector & zk
    )
{
    Info << "Fixed relaxation post processing" << endl;

    xk += zstar - zk;
}

void ASMILS::performPostProcessing(
    const vector & y,
    const vector & x0,
    vector & xk,
    bool residualCriterium
    )
{
    assert( x0.rows() == xk.rows() );

    // Initialize variables

    int m = y.rows();
    vector output( m ), R( m ), zstar( m ), zk( m ), xkprev( m );
    xk = x0;
    xkprev = x0;
    coarseResiduals.clear();
    fineResiduals.clear();

    // Determine optimum of coarse model zstar
    if ( residualCriterium )
    {
        assert( y.norm() < 1.0e-14 );
        coarseModel->optimize( x0, zstar );
    }

    if ( !residualCriterium )
        coarseModel->optimize( y, x0, zstar );

    if ( !coarseModel->allConverged() )
        Warning << "ASMILS: surrogate model optimization process is not converged." << endl;

    xk = zstar;

    // Fine model evaluation

    fineModel->evaluate( xk, output, R );

    // Check convergence criteria
    if ( isConvergence( xk, xkprev, residualCriterium ) )
    {
        iterationsConverged();
        return;
    }

    fineResiduals.push_back( xk );

    // Parameter extraction

    coarseModel->optimize( R - y, zstar, zk );

    if ( !coarseModel->allConverged() )
        Warning << "Surrogate model optimization process is not converged." << endl;

    coarseResiduals.push_back( zk );

    for ( int k = 0; k < maxIter - 1; k++ )
    {
        xkprev = xk;

        // Determine the number of columns used to calculate the mapping matrix J

        int nbCols = fineResiduals.size() - 1;
        nbCols = std::max( nbCols, 0 );

        // Include information from previous optimization cycles

        for ( auto && fineResiduals : fineResidualsList )
            nbCols += fineResiduals.size() - 1;

        // Include information from previous time steps

        for ( auto && fineResidualsList : fineResidualsTimeList )
            for ( auto && fineResiduals : fineResidualsList )
                nbCols += fineResiduals.size() - 1;

        nbCols = std::min( static_cast<int>( xk.rows() ), nbCols );
        nbCols = std::min( nbCols, maxUsedIterations );

        assert( nbCols <= xk.rows() );

        // Use fixed under relaxation to startup the IQNILS algorithm
        if ( nbCols == 0 )
            fixedUnderRelaxation( xk, zstar, zk );

        if ( nbCols > 0 )
        {
            // IQNILS algorithm

            Info << "ASM-ILS post processing with " << nbCols << " cols for the Jacobian" << endl;

            // Construct the V and W matrices
            matrix V( xk.rows(), nbCols ), W( xk.rows(), nbCols );

            int nbColsCurrentTimeStep = std::max( static_cast<int>(fineResiduals.size() - 1), 0 );
            nbColsCurrentTimeStep = std::min( nbColsCurrentTimeStep, nbCols );

            // Include information from previous iterations

            int colIndex = 0;

            for ( int i = 0; i < nbColsCurrentTimeStep; i++ )
            {
                if ( colIndex >= V.cols() )
                    continue;

                V.col( i ) = coarseResiduals.at( coarseResiduals.size() - 2 - i ) - coarseResiduals.back();
                W.col( i ) = fineResiduals.at( fineResiduals.size() - 2 - i ) - fineResiduals.back();
                colIndex++;
            }

            // Include information from previous optimization cycles

            for ( unsigned i = 0; i < fineResidualsList.size(); i++ )
            {
                assert( fineResidualsList.at( i ).size() >= 2 );
                assert( coarseResidualsList.at( i ).size() >= 2 );

                for ( unsigned j = 0; j < fineResidualsList.at( i ).size() - 1; j++ )
                {
                    if ( colIndex >= V.cols() )
                        continue;

                    V.col( colIndex ) = coarseResidualsList.at( i ).at( coarseResidualsList.at( i ).size() - 2 - j ) - coarseResidualsList.at( i ).back();
                    W.col( colIndex ) = fineResidualsList.at( i ).at( fineResidualsList.at( i ).size() - 2 - j ) - fineResidualsList.at( i ).back();
                    colIndex++;
                }
            }

            // Include information from previous time steps

            for ( unsigned i = 0; i < fineResidualsTimeList.size(); i++ )
            {
                for ( unsigned j = 0; j < fineResidualsTimeList.at( i ).size(); j++ )
                {
                    assert( fineResidualsTimeList.at( i ).at( j ).size() >= 2 );
                    assert( coarseResidualsTimeList.at( i ).at( j ).size() >= 2 );

                    for ( unsigned k = 0; k < fineResidualsTimeList.at( i ).at( j ).size() - 1; k++ )
                    {
                        if ( colIndex >= V.cols() )
                            continue;

                        V.col( colIndex ) = coarseResidualsTimeList.at( i ).at( j ).at( coarseResidualsTimeList.at( i ).at( j ).size() - 2 - k ) - coarseResidualsTimeList.at( i ).at( j ).back();
                        W.col( colIndex ) = fineResidualsTimeList.at( i ).at( j ).at( fineResidualsTimeList.at( i ).at( j ).size() - 2 - k ) - fineResidualsTimeList.at( i ).at( j ).back();
                        colIndex++;
                    }
                }
            }

            assert( colIndex == nbCols );
            assert( V.cols() == W.cols() );

            // Truncated singular value decomposition to solve for the
            // coefficients

            Eigen::JacobiSVD<matrix> svd( V, Eigen::ComputeThinU | Eigen::ComputeThinV );

            vector singularValues_inv = svd.singularValues();

            for ( unsigned int i = 0; i < singularValues_inv.rows(); ++i )
            {
                if ( svd.singularValues() ( i ) > singularityLimit )
                    singularValues_inv( i ) = 1.0 / svd.singularValues() ( i );
                else
                    singularValues_inv( i ) = 0;
            }

            vector c = svd.matrixV() * ( singularValues_inv.asDiagonal() * ( svd.matrixU().transpose() * (zstar - zk) ) );

            // Update solution x
            xk += beta * (zk - zstar) + W * c + beta * V * c;
        }

        // Fine model evaluation

        fineModel->evaluate( xk, output, R );

        // Check convergence criteria
        if ( isConvergence( xk, xkprev, residualCriterium ) )
        {
            iterationsConverged();
            break;
        }

        fineResiduals.push_back( xk );

        // Parameter extraction

        coarseModel->optimize( R - y, zstar, zk );

        if ( !coarseModel->allConverged() )
            Warning << "ASMILS: surrogate model optimization process is not converged." << endl;

        coarseResiduals.push_back( zk );
    }
}
