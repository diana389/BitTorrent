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

#define UPLOAD_TAG 5
#define FILE_INFO_TAG 0
#define TRACKER_REQUEST_TAG 2
#define TRACKER_RESPONSE_TAG 3
#define CHUNK_TAG 15

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

typedef struct {
    char filename[MAX_FILENAME];
    int chunks_recv[MAX_CHUNKS];
    int chunks_count_recv;
    chunk_info chunks[MAX_CHUNKS];
} file_requested;

file_requested files_requested[MAX_FILES];
int files_requested_count = 0;
int files_requested_completed = 0;

file_info files_owned[MAX_FILES];
int files_owned_count = 0;

int numtasks;

void create_client_file(int rank, char *filename, char *hash, int chunk_index) {
        char clientfilename[MAX_FILENAME] = "client0_";
        clientfilename[6] = rank + '0';
        strcat(clientfilename, filename);

        // hash[HASH_SIZE + 1] = '\n';
        // hash[HASH_SIZE + 2] = '\0';

        if(chunk_index == 0) {
            FILE *output = fopen(clientfilename, "w");
            fwrite(hash, sizeof(char), HASH_SIZE, output);
            fwrite("\n", sizeof(char), 1, output);
            fclose(output);
        }
        else {
            FILE *output = fopen(clientfilename, "a");
            fwrite(hash, sizeof(char), HASH_SIZE, output);
            fwrite("\n", sizeof(char), 1, output);
            fclose(output);
        }
}

file_info request_file_info_from_tracker(int rank, char *filename) {
        printf("Peer %d requests file %s\n", rank, filename);

        int owners_count = 0;
        int owners[100];

        char owners_request[15] = "OWNERS REQ";
        MPI_Send(owners_request, 15, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);

        MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);

        file_info current_file;
        MPI_Recv(&current_file, sizeof(file_info), MPI_BYTE, TRACKER_RANK, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        printf("FILE RECEIVED: %s\n", current_file.filename);

        for(int j = 0; j < current_file.seed_count; j++) {
            printf("Seed %d\n", current_file.seed[j]);
        }

        return current_file;
}

void request_chunk_from_peer(int rank, char *filename, int file_index, file_info current_file, char *hash, int chunk_index) {
        int chosen_owner = current_file.chunks[chunk_index].owners[0];

        // printf("Chunk %d: owner %d\n", current_file.chunks[chunk_index].index, chosen_owner);

        char chunk_request[15] = "CHUNK REQ";
        MPI_Send(chunk_request, 15, MPI_CHAR, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);
        MPI_Send(files_requested[file_index].filename, MAX_FILENAME, MPI_CHAR, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);
        MPI_Send(&current_file.chunks[chunk_index].index, 1, MPI_INT, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);

        MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, chosen_owner, CHUNK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // printf("Chunk %d: %s\n", current_file.chunks[chunk_index].index, hash);

        // chunk_index = current_file.chunks[chunk_index].index;
        files_requested[file_index].chunks_recv[chunk_index] = chosen_owner;
        files_requested[file_index].chunks_count_recv++;
        strcpy(files_requested[file_index].chunks[chunk_index].hash, hash);
        files_requested[file_index].chunks[chunk_index].index = chunk_index;
}

void manage_file_requested(int rank, char *filename, int file_index) {
        file_info current_file = request_file_info_from_tracker(rank, filename);

        char hash[HASH_SIZE + 1];

        printf("File %s has %d chunks and %d chunks\n", current_file.filename, current_file.chunks_count, files_requested[file_index].chunks_count_recv);

        for(int i = 0; i < current_file.chunks_count; i++) {
            request_chunk_from_peer(rank, filename, file_index, current_file, hash, i);
        }
        // request_chunk_from_peer(rank, filename, file_index, current_file, hash, 0);

            // printf("!!!!!!Hash: %s\n", hash);

        if(files_requested[file_index].chunks_count_recv == current_file.chunks_count) {
            printf("File %s completed\n", filename);

            for (int i = 0; i < files_requested[file_index].chunks_count_recv; i++) {
                create_client_file(rank, filename, files_requested[file_index].chunks[i].hash, i);
            }
            
            files_requested_completed++;
        }
}

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;
    printf("Hello from download thread %d\n", rank);

    // if(rank != 3)
    //     return NULL;
    
    while(files_requested_completed < files_requested_count) {
            for(int i = 0; i < files_requested_count; i++) {
                manage_file_requested(rank, files_requested[i].filename, i);
            }
    }

    printf("Ready request from peer %d\n", rank);
    char ready[15] = "READY";
    MPI_Send(ready, 15, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    printf("Hello from upload thread %d\n", rank);

    while (1) {    
        MPI_Status status;
        char request[15] = "";

        MPI_Recv(request, 15, MPI_CHAR, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status);

        printf("Request received: %s from process %d\n", request, status.MPI_SOURCE);

        if (strcmp(request, "CHUNK REQ") == 0) {
            char filename[MAX_FILENAME];
            int chunk_index = -1;

            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&chunk_index, 1, MPI_INT, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            printf("CHUNK REQ from peer %d for file %s chunk %d\n", status.MPI_SOURCE, filename, chunk_index);

            for(int i = 0; i < files_owned_count; i++) {
                if (strcmp(files_owned[i].filename, filename) == 0) {
                    // printf("chunk %d: %s\n", chunk_index, files_owned[i].chunks[chunk_index].hash);

                    MPI_Send(files_owned[i].chunks[chunk_index].hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);
                }
            }
        }

        if (strcmp(request, "DONE") == 0) {
            printf("DONE from peer %d\n", rank);
            return NULL;
        }
    }
    

    return NULL;
}

void add_files_to_tracker_database(int numtasks, int rank) {
    int files_owned, chunks;
    char filename[MAX_FILENAME];
    char hash[HASH_SIZE];

    for(int i = 1; i < numtasks; i++) {
        MPI_Recv(&files_owned, 1, MPI_INT, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // printf("Peer %d owns %d files\n", i, files_owned);

        for(int j = 0; j < files_owned; j++) {
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // printf("Peer %d owns file %s\n", i, filename);

            MPI_Recv(&chunks, 1, MPI_INT, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            files[files_count].seed_count = 0;
            files[files_count].seed[files[files_count].seed_count++] = i;
            strcpy(files[files_count].filename, filename);
            files[files_count].chunks_count = 0;

            for(int k = 0; k < chunks; k++) {
                MPI_Recv(hash, HASH_SIZE, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                printf("Peer %d owns chunk %s from file %s index %d\n", i, hash, filename, k);

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
        MPI_Send(response, 10, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD);
    }
}

void send_done_message(int numtasks) {
    char response[10] = "DONE";
    for(int i = 1; i < numtasks; i++) {
        MPI_Send(response, 10, MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD);
    }

    printf("DONE sent to all peers\n");
}

void tracker(int numtasks, int rank) {
    // printf("Hello from tracker %d\n", rank);

    add_files_to_tracker_database(numtasks, rank);


    int clients_ready[numtasks];
    for(int i = 0; i < numtasks; i++) {
        clients_ready[i] = 0;
    }

    while(1) {
        char request[15];
        MPI_Status status;

        MPI_Recv(request, 15, MPI_CHAR, MPI_ANY_SOURCE, TRACKER_REQUEST_TAG, MPI_COMM_WORLD, &status);

        if (strcmp(request, "OWNERS REQ") == 0) {
            char filename_req[MAX_FILENAME];
            MPI_Recv(filename_req, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, TRACKER_REQUEST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            printf("OWNERS REQ from peer %d for file %s\n", status.MPI_SOURCE, filename_req);

            for(int i = 0; i < files_count; i++) {
                if (strcmp(files[i].filename, filename_req) == 0) {
                    MPI_Send(&files[i], sizeof(file_info), MPI_BYTE, status.MPI_SOURCE, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD);
                }
            }
        }

        if(strcmp(request, "READY") == 0) {
            clients_ready[status.MPI_SOURCE] = 1;
            printf("Peer %d is ready\n", status.MPI_SOURCE);

            int all_ready = 1;
            for(int i = 1; i < numtasks; i++) {
                if(clients_ready[i] == 0) {
                    all_ready = 0;
                    break;
                }
            }

            if(all_ready == 1) {            
                send_done_message(numtasks);
                return;
            }

        }
    }
}

void peer(int numtasks, int rank) {
    numtasks = numtasks;

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

    int chunks;
    char filename[MAX_FILENAME], hash[HASH_SIZE];

    fscanf(input, "%d", &files_owned_count);

    MPI_Send(&files_owned_count, 1, MPI_INT, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

    for(int i = 0; i < files_owned_count; i++) {
        fscanf(input, "%s", filename);
        strcpy(files_owned[i].filename, filename);
        // printf("Peer %d owns file %s\n", rank, filename);

        MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

        fscanf(input, "%d", &chunks);
        files_owned[i].chunks_count = chunks;
        MPI_Send(&chunks, 1, MPI_INT, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

        for(int j = 0; j < chunks; j++) {
            fscanf(input, "%s", hash);
            hash[HASH_SIZE] = '\0';
            strcpy(files_owned[i].chunks[j].hash, hash);

            MPI_Send(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);
            // printf("Peer %d owns chunk %s\n", rank, hash);
        }
    }

    fscanf(input, "%d", &files_requested_count);

    for(int i = 0; i < files_requested_count; i++) {
        fscanf(input, "%s", filename);
        printf("Peer %d requests file %s\n", rank, filename);

        strcpy(files_requested[i].filename, filename);

        for(int j = 0; j < MAX_CHUNKS; j++) {
            files_requested[i].chunks_recv[j] = -1;
        }
    }

    char response[10];
    MPI_Recv(response, 10, MPI_CHAR, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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
