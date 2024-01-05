#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

typedef struct {
    char hash[HASH_SIZE];
    int owners[100];
    int owners_count;
    int index;
} chunk_info;

typedef struct {
    char filename[MAX_FILENAME];
    chunk_info chunks[MAX_CHUNKS];
    int chunks_count;
    int seed[100];
    int seed_count;
} file_info;


file_info files[MAX_FILES];
int files_count = 0;

MPI_Comm new_comm;

char files_requested[MAX_FILES][MAX_FILENAME];
int files_requested_count = 0;

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;
    printf("Hello from download thread %d\n", rank);

    for(int i = 0; i < files_requested_count; i++) {
        printf("Peer %d requests file %s\n", rank, files_requested[i]);

        int oweners_count = 0;
        int owners[100];

        char owners_request[15] = "OWNERS REQ";
        MPI_Send(owners_request, 15, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        MPI_Send(files_requested[i], MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    }

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    printf("Hello from upload thread %d\n", rank);

    return NULL;
}

void tracker(int numtasks, int rank) {
    // printf("Hello from tracker %d\n", rank);

    int files_owned, chunks;
    char filename[MAX_FILENAME];
    char hash[HASH_SIZE];

    for(int i = 1; i < numtasks; i++) {
        MPI_Recv(&files_owned, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // printf("Peer %d owns %d files\n", i, files_owned);

        for(int j = 0; j < files_owned; j++) {
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // printf("Peer %d owns file %s\n", i, filename);

            MPI_Recv(&chunks, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            files[files_count].seed_count = 0;
            files[files_count].seed[files[files_count].seed_count++] = i;
            strcpy(files[files_count].filename, filename);
            files[files_count].chunks_count = 0;

            for(int k = 0; k < chunks; k++) {
                MPI_Recv(hash, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                // printf("Peer %d owns chunk %s\n", i, hash);

                chunk_info current_chunk;
                strcpy(current_chunk.hash, hash);
                current_chunk.owners_count = 0;
                current_chunk.owners[current_chunk.owners_count++] = i;
                current_chunk.index = k;

                files[files_count].chunks[files[files_count].chunks_count++] = current_chunk;
            }

            files_count++;
        }
    }

    for(int i = 0; i < files_count; i++) {
        for(int j = 0; j < files[i].seed_count; j++) {
            printf("File %s seed %d\n", files[i].filename, files[i].seed[j]);
        }

        printf("Chunks: %d\n", files[i].chunks_count);

        // for(int j = 0; j < files[i].chunks_count; j++) {
        //     printf("Chunk %d: %s\n", j, files[i].chunks[j].hash);
        // }
    }

    for(int i = 1; i < numtasks; i++) {
        char response[10] = "FILES OK";
        MPI_Send(response, 10, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }

    while(1) {
        char request[15];
        MPI_Status status;

        MPI_Recv(request, 15, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        if (strcmp(request, "OWNERS REQ") == 0) {
            char filename_req[MAX_FILENAME];
            MPI_Recv(filename_req, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            printf("OWNERS REQ from peer %d for file %s\n", status.MPI_SOURCE, filename_req);
        }
    }
}

void peer(int numtasks, int rank) {
    char infilename[MAX_FILENAME] = "in";
    char rank_str[5];
    snprintf(rank_str, sizeof(int), "%d", rank);

    strcat(infilename, rank_str);
    strcat(infilename, ".txt");

    printf("%s\n", infilename);

    FILE *input = fopen(infilename, "r");

    if (input == NULL) {
        printf("Eroare la deschiderea fisierului de input\n");
        exit(-1);
    }

    int files_owned, files_requested_num, chunks;
    char filename[MAX_FILENAME], hash[HASH_SIZE];

    fscanf(input, "%d", &files_owned);

    MPI_Send(&files_owned, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

    for(int i = 0; i < files_owned; i++) {
        fscanf(input, "%s", filename);
        // printf("Peer %d owns file %s\n", rank, filename);

        MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        fscanf(input, "%d", &chunks);
        MPI_Send(&chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

        for(int j = 0; j < chunks; j++) {
            fscanf(input, "%s", hash);
            hash[HASH_SIZE - 1] = '\0';

            MPI_Send(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
            // printf("Peer %d owns chunk %s\n", rank, hash);
        }
    }

    fscanf(input, "%d", &files_requested_num);

    for(int i = 0; i < files_requested_num; i++) {
        fscanf(input, "%s", filename);
        printf("Peer %d requests file %s\n", rank, filename);
        strcpy(files_requested[files_requested_count++], filename);
    }

    char response[10];
    MPI_Recv(response, 10, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (strcmp(response, "FILES OK") == 0) {
        printf("FILES OK from peer %d\n", rank);
    }
    else {
        printf("FILES NOT OK from peer %d\n", rank);
        exit(-1);
    }

    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // make a new communicator of all worker
    // if (rank != TRACKER_RANK) {
    //     MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
    // } else {
    //     MPI_Comm_split(MPI_COMM_WORLD, MPI_UNDEFINED, rank, &new_comm);
    // }

    // if (new_comm == MPI_COMM_NULL) {
    //     printf("New communicator is null\n");
    //     MPI_Finalize(); 
    //     exit(0);
    // }

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    // MPI_Comm_free(&new_comm);

    MPI_Finalize();
}
