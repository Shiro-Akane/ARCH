/* Experiment-only PMPI observer. Not linked into ARCH; no extra communication. */
#define _POSIX_C_SOURCE 200809L
#include <mpi.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

struct observation { uint64_t calls, sent_payload_bytes; double seconds; };
static struct observation records[7];
static const char *names[] = {"Sendrecv", "Allgather", "Allreduce", "Bcast", "Irecv", "Isend", "Waitall"};
static const char *directory;
static int rank_id, world_size;
static double initialized_at, initialize_seconds;
static double durations[7][32768], starts[7][32768];

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * t.tv_nsec;
}
static void record(int operation, double started, int count, MPI_Datatype type, int sends) {
    double elapsed = now() - started;
    int bytes = 0;
    PMPI_Type_size(type, &bytes);
    if (records[operation].calls < 32768) {
        durations[operation][records[operation].calls] = elapsed;
        starts[operation][records[operation].calls] = started - initialized_at;
    }
    records[operation].calls++;
    records[operation].seconds += elapsed;
    if (sends && count > 0)
        records[operation].sent_payload_bytes += (uint64_t)count * bytes;
}
int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) {
    double started = now();
    int result = PMPI_Init_thread(argc, argv, required, provided);
    initialize_seconds = now() - started;
    if (result == MPI_SUCCESS) {
        directory = getenv("ARCH_MPI_PROFILE_DIR");
        PMPI_Comm_rank(MPI_COMM_WORLD, &rank_id);
        PMPI_Comm_size(MPI_COMM_WORLD, &world_size);
        initialized_at = now();
    }
    return result;
}
int MPI_Sendrecv(const void *send, int sc, MPI_Datatype st, int destination, int stag,
                 void *receive, int rc, MPI_Datatype rt, int source, int rtag,
                 MPI_Comm comm, MPI_Status *status) {
    double started = directory ? now() : 0.;
    int result = PMPI_Sendrecv(send, sc, st, destination, stag, receive, rc, rt, source, rtag, comm, status);
    if (directory) record(0, started, sc, st, destination != MPI_PROC_NULL);
    return result;
}
int MPI_Irecv(void *receive, int count, MPI_Datatype type, int source, int tag,
              MPI_Comm comm, MPI_Request *request) {
    double started = directory ? now() : 0.;
    int result = PMPI_Irecv(receive, count, type, source, tag, comm, request);
    if (directory) record(4, started, count, type, 0);
    return result;
}
int MPI_Isend(const void *send, int count, MPI_Datatype type, int destination, int tag,
              MPI_Comm comm, MPI_Request *request) {
    double started = directory ? now() : 0.;
    int result = PMPI_Isend(send, count, type, destination, tag, comm, request);
    if (directory) record(5, started, count, type, destination != MPI_PROC_NULL);
    return result;
}
int MPI_Waitall(int count, MPI_Request requests[], MPI_Status statuses[]) {
    double started = directory ? now() : 0.;
    int result = PMPI_Waitall(count, requests, statuses);
    if (directory) record(6, started, 0, MPI_BYTE, 0);
    return result;
}
int MPI_Allgather(const void *send, int sc, MPI_Datatype st, void *receive, int rc,
                  MPI_Datatype rt, MPI_Comm comm) {
    double started = directory ? now() : 0.;
    int result = PMPI_Allgather(send, sc, st, receive, rc, rt, comm);
    if (directory) record(1, started, sc, st, 1);
    return result;
}
int MPI_Allreduce(const void *send, void *receive, int count, MPI_Datatype type,
                  MPI_Op op, MPI_Comm comm) {
    double started = directory ? now() : 0.;
    int result = PMPI_Allreduce(send, receive, count, type, op, comm);
    if (directory) record(2, started, count, type, 1);
    return result;
}
int MPI_Bcast(void *buffer, int count, MPI_Datatype type, int root, MPI_Comm comm) {
    double started = directory ? now() : 0.;
    int result = PMPI_Bcast(buffer, count, type, root, comm);
    if (directory) record(3, started, count, type, rank_id == root);
    return result;
}
int MPI_Finalize(void) {
    double active = now() - initialized_at, started = now();
    int result = PMPI_Finalize();
    double finalized = now() - started;
    if (directory) {
        char path[32768], host[256] = {0};
        gethostname(host, sizeof(host)-1);
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        snprintf(path, sizeof(path), "%s/rank-%d/profile.json", directory, rank_id);
        FILE *f = fopen(path, "w");
        if (!f) { perror("MPI observation output"); return result; }
        fprintf(f, "{\"rank\":%d,\"size\":%d,\"host\":\"%s\","
            "\"init_seconds\":%.17g,\"active_seconds\":%.17g,\"finalize_seconds\":%.17g,"
            "\"max_rss_kib\":%ld,\"operations\":{", rank_id,world_size,host,
            initialize_seconds,active,finalized,usage.ru_maxrss);
        for (int i=0;i<7;i++)
            fprintf(f,"%s\"%s\":{\"calls\":%" PRIu64 ",\"seconds\":%.17g,"
                "\"sent_payload_bytes\":%" PRIu64 "}",
                i?",":"",names[i],records[i].calls,records[i].seconds,records[i].sent_payload_bytes);
        fprintf(f,"},\"initialized_monotonic\":%.17g,\"trace\":{",initialized_at);
        for (int op=0; op<7; ++op) {
            fprintf(f,"%s\"%s\":[", op?",":"",names[op]);
            uint64_t length = records[op].calls < 32768 ? records[op].calls : 32768;
            for (uint64_t i=0; i<length; ++i)
                fprintf(f,"%s[%.17g,%.17g]",i?",":"",starts[op][i],durations[op][i]);
            fputc(']',f);
        }
        fputs("}}\n",f);
        fclose(f);
    }
    return result;
}
