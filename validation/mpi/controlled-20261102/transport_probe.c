/* Isolated two-rank transport measurement; never linked into ARCH. */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank=0, size=0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size!=2) { MPI_Finalize(); return 2; }
    const int sizes[]={96,32768,262144};
    unsigned char *send=malloc(262144), *receive=malloc(262144);
    if (!send || !receive) MPI_Abort(MPI_COMM_WORLD,3);
    for (int i=0;i<262144;++i) send[i]=(unsigned char)(rank+i);
    for (int k=0;k<3;++k) {
        const int count=sizes[k], repeats=count==96?1000:100;
        for (int warm=0;warm<20;++warm)
            MPI_Sendrecv(send,count,MPI_BYTE,1-rank,0,receive,count,MPI_BYTE,1-rank,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
        for (int sample=0;sample<5;++sample) {
            MPI_Barrier(MPI_COMM_WORLD);
            double start=MPI_Wtime();
            for (int i=0;i<repeats;++i)
                MPI_Sendrecv(send,count,MPI_BYTE,1-rank,0,receive,count,MPI_BYTE,1-rank,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            double local=MPI_Wtime()-start, maximum=0;
            MPI_Reduce(&local,&maximum,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
            for (int i=0;i<count;++i)
                if (receive[i]!=(unsigned char)(1-rank+i)) MPI_Abort(MPI_COMM_WORLD,4);
            if (rank==0) printf("{\"bytes_per_rank_per_call\":%d,\"sample\":%d,\"calls\":%d,\"max_rank_seconds_per_call\":%.17g}\n",count,sample,repeats,maximum/repeats);
        }
    }
    free(send);free(receive);
    MPI_Finalize();
    return 0;
}
