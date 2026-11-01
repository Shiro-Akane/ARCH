#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
 int supplied, rank, size, sum, value;
 MPI_Init_thread(&argc,&argv,MPI_THREAD_FUNNELED,&supplied);
 MPI_Comm_rank(MPI_COMM_WORLD,&rank);MPI_Comm_size(MPI_COMM_WORLD,&size);
 char host[256]={0};gethostname(host,255);
 value=rank+1;MPI_Allreduce(&value,&sum,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
 printf("rank=%d size=%d host=%s sum=%d\n",rank,size,host,sum);fflush(stdout);
 if(sum!=size*(size+1)/2)MPI_Abort(MPI_COMM_WORLD,2);
 MPI_Finalize();return 0;
}
