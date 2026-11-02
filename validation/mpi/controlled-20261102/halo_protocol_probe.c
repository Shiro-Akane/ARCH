/* Compare the exact two-direction halo protocols without changing production code. */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

static void exchange(int concurrent, int lower, int upper, int count,
                     unsigned char *send_lower, unsigned char *send_upper,
                     unsigned char *receive_lower, unsigned char *receive_upper) {
    if (!concurrent) {
        MPI_Sendrecv(send_upper,count,MPI_BYTE,upper,310,receive_lower,count,MPI_BYTE,lower,310,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
        MPI_Sendrecv(send_lower,count,MPI_BYTE,lower,311,receive_upper,count,MPI_BYTE,upper,311,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    } else {
        MPI_Request requests[4];
        MPI_Irecv(receive_lower,count,MPI_BYTE,lower,310,MPI_COMM_WORLD,&requests[0]);
        MPI_Irecv(receive_upper,count,MPI_BYTE,upper,311,MPI_COMM_WORLD,&requests[1]);
        MPI_Isend(send_upper,count,MPI_BYTE,upper,310,MPI_COMM_WORLD,&requests[2]);
        MPI_Isend(send_lower,count,MPI_BYTE,lower,311,MPI_COMM_WORLD,&requests[3]);
        MPI_Waitall(4,requests,MPI_STATUSES_IGNORE);
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc,&argv);
    int rank=0,size=0;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);MPI_Comm_size(MPI_COMM_WORLD,&size);
    if (size!=2 && size!=4) { MPI_Finalize();return 2; }
    const int lower=(rank+size-1)%size, upper=(rank+1)%size;
    const int sizes[]={24576,196608}, capacity=196608;
    unsigned char *storage=malloc((size_t)capacity*4);
    if (!storage) MPI_Abort(MPI_COMM_WORLD,3);
    unsigned char *sl=storage,*su=sl+capacity,*rl=su+capacity,*ru=rl+capacity;
    for (int i=0;i<capacity;++i) {sl[i]=(unsigned char)(rank+i);su[i]=(unsigned char)(rank+i+17);}
    for (int k=0;k<2;++k) {
        const int count=sizes[k],iterations=100;
        for (int sample=-1;sample<5;++sample) {
            for (int order=0;order<2;++order) {
                const int concurrent=(order+(sample+1)%2)%2;
                MPI_Barrier(MPI_COMM_WORLD);
                double started=MPI_Wtime();
                for (int i=0;i<iterations;++i) exchange(concurrent,lower,upper,count,sl,su,rl,ru);
                double elapsed=MPI_Wtime()-started,maximum=0;
                MPI_Reduce(&elapsed,&maximum,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
                for (int i=0;i<count;++i)
                    if (rl[i]!=(unsigned char)(lower+i+17) || ru[i]!=(unsigned char)(upper+i))
                        MPI_Abort(MPI_COMM_WORLD,4);
                if (rank==0 && sample>=0)
                    printf("{\"ranks\":%d,\"bytes_per_direction\":%d,\"sample\":%d,\"concurrent\":%s,\"exchanges\":%d,\"max_rank_seconds_per_exchange\":%.17g}\n",size,count,sample,concurrent?"true":"false",iterations,maximum/iterations);
            }
        }
    }
    free(storage);MPI_Finalize();return 0;
}
