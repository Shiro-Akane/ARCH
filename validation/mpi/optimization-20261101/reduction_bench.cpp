#include "driver/DriverUtils.h"
#include <mpi.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdlib>

int main(int argc, char** argv) {
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 20000;
    const auto key = DriverReduction::make_block_reduction_key(
        0, rank, size - rank, 0, 0, DriverReduction::BlockReductionComponent::Hydro);
    const std::vector<arch::reduction::ReductionCandidate> candidates{{double(10-rank), key, true}};
    for (int i=0; i<1000; ++i)
        (void)DriverReduction::reduce_block_minimum(1e99, candidates, size);
    for (int sample=0; sample<5; ++sample) {
        MPI_Barrier(MPI_COMM_WORLD);
        const double started = MPI_Wtime();
        double value = 0.;
        for (int i=0; i<iterations; ++i)
            value = DriverReduction::reduce_block_minimum(1e99, candidates, size);
        const double elapsed = MPI_Wtime()-started;
        if (value != 11.-size) MPI_Abort(MPI_COMM_WORLD, 2);
        double critical = 0.;
        MPI_Reduce(&elapsed, &critical, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        if (rank == 0)
            std::cout << std::setprecision(17) << "{\"ranks\":" << size
                << ",\"sample\":" << sample << ",\"iterations\":" << iterations
                << ",\"seconds\":" << critical << "}\n";
    }
    MPI_Finalize();
}
