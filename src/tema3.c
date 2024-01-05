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
    char filename[MAX_FILENAME];
    char chunks[MAX_CHUNKS][HASH_SIZE];
    int chunks_count;
    int seed;
} file_info;

file_info files[MAX_FILES];
int files_count = 0;

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;
    // printf("Hello from download thread %d\n", rank);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    // printf("Hello from upload thread %d\n", rank);

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

            files[files_count].seed = i;
            strcpy(files[files_count].filename, filename);
            files[files_count].chunks_count = 0;

            for(int k = 0; k < chunks; k++) {
                MPI_Recv(hash, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                // printf("Peer %d owns chunk %s\n", i, hash);

                strcpy(files[files_count].chunks[files[files_count].chunks_count++], hash);
            }

            files_count++;
        }
    }

    for(int i = 0; i < files_count; i++) {
        printf("File %s owned by peer %d\n", files[i].filename, files[i].seed);

        printf("Chunks: %d\n", files[i].chunks_count);

        // for(int j = 0; j < files[i].chunks_count; j++) {
        //     printf("Chunk %s\n", files[i].chunks[j]);
        // }
    }

    for(int i = 1; i < numtasks; i++) {
        char response[10] = "FILES OK";
        MPI_Send(response, 10, MPI_CHAR, i, 0, MPI_COMM_WORLD);
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

    int files_owned, files_requested, chunks;
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

    fscanf(input, "%d", &files_requested);

    for(int i = 0; i < files_requested; i++) {
        fscanf(input, "%s", filename);
        printf("Peer %d requests file %s\n", rank, filename);
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

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
