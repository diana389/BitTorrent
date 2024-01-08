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


file_info files[MAX_FILES]; // files database on tracker
int files_count = 0; // number of files in database

typedef struct {
    char filename[MAX_FILENAME];
    int chunks_recv[MAX_CHUNKS];
    int chunks_count_recv;
    chunk_info chunks[MAX_CHUNKS];
} file_requested;

file_requested files_requested[MAX_FILES]; // files requested by peer
int files_requested_count = 0; // number of files requested by peer
int files_requested_completed = 0; // number of files requested by peer and completed

file_info files_owned[MAX_FILES]; // files owned by peer
int files_owned_count = 0; // number of files owned by peer

int numtasks;

// function to create client file
void create_client_file(int rank, char *filename, char *hash, int chunk_index) {
        // compute client file name
        char clientfilename[MAX_FILENAME] = "client0_";
        clientfilename[6] = rank + '0';
        strcat(clientfilename, filename);

        if(chunk_index == 0) {
            // create new file
            FILE *output = fopen(clientfilename, "w");
            fwrite(hash, sizeof(char), HASH_SIZE, output);
            fwrite("\n", sizeof(char), 1, output);
            fclose(output);
        }
        else {
            // append to existing file
            FILE *output = fopen(clientfilename, "a");
            fwrite(hash, sizeof(char), HASH_SIZE, output);
            fwrite("\n", sizeof(char), 1, output);
            fclose(output);
        }
}

// function to request chunks information from tracker
void request_file_info_from_tracker(int rank, char *filename, chunk_info *chunks, int *chunks_count) {
        printf("Peer %d requests file %s\n", rank, filename);

        // send request type to tracker
        char owners_request[15] = "OWNERS REQ";
        MPI_Send(owners_request, 15, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);
        // send filename to tracker
        MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);

        // receive chunks count from tracker
        MPI_Recv(chunks_count, 1, MPI_INT, TRACKER_RANK, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        for(int i = 0; i < *chunks_count; i++) {
            // receive chunk owners number from tracker
            MPI_Recv(&chunks[i].owners_count, 1, MPI_INT, TRACKER_RANK, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receive chunk index from tracker
            MPI_Recv(&chunks[i].index, 1, MPI_INT, TRACKER_RANK, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receive chunk owners from tracker
            MPI_Recv(chunks[i].owners, chunks[i].owners_count, MPI_INT, TRACKER_RANK, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
}

// function to request chunk from peer
void request_chunk_from_peer(int rank, char *filename, int file_index, chunk_info *chunks, char *hash, int chunk_index) {
        int chosen_owner = chunks[chunk_index].owners[0]; // choose owner of chunk

        printf("Chunk %d: owner %d\n", chunks[chunk_index].index, chosen_owner);

        // send chunk request to chosen owner
        char chunk_request[15] = "CHUNK REQ";
        MPI_Send(chunk_request, 15, MPI_CHAR, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);
        // send filename to chosen owner
        MPI_Send(files_requested[file_index].filename, MAX_FILENAME, MPI_CHAR, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);
        // send chunk index to chosen owner
        MPI_Send(&chunks[chunk_index].index, 1, MPI_INT, chosen_owner, UPLOAD_TAG, MPI_COMM_WORLD);

        // receive chunk hash from chosen owner
        MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, chosen_owner, CHUNK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // add chunk to file structure
        files_requested[file_index].chunks_recv[chunk_index] = chosen_owner;
        files_requested[file_index].chunks_count_recv++;
        strcpy(files_requested[file_index].chunks[chunk_index].hash, hash);
        files_requested[file_index].chunks[chunk_index].index = chunk_index;
}

// function to try and complete file
void manage_file_requested(int rank, char *filename, int file_index) {
        chunk_info chunks[MAX_CHUNKS];
        int chunks_count = 0;

        // get chunks and chunks count from tracker
        request_file_info_from_tracker(rank, filename, chunks, &chunks_count);
        
        printf("File %s should have %d chunks and has %d chunks\n", filename, chunks_count, files_requested[file_index].chunks_count_recv);

        // request chunks from peers
        for(int i = 0; i < chunks_count; i++) {
            char hash[HASH_SIZE + 1];
            request_chunk_from_peer(rank, filename, file_index, chunks, hash, i);
        }

        // check if file is completed
        if(files_requested[file_index].chunks_count_recv == chunks_count) {
            printf("File %s completed\n", filename);

            // create client file
            for (int i = 0; i < files_requested[file_index].chunks_count_recv; i++) {
                create_client_file(rank, filename, files_requested[file_index].chunks[i].hash, i);
            }
            
            files_requested_completed++;
        }
}

// function to be executed by download thread
void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;
    
    // make requests to tracker until all files are completed
    while(files_requested_completed < files_requested_count) {
        for(int i = 0; i < files_requested_count; i++) {
            manage_file_requested(rank, files_requested[i].filename, i);
        }
    }

    // send ready request to tracker when all files are completed
    char ready[15] = "READY";
    printf("Ready request from peer %d\n", rank);
    MPI_Send(ready, 15, MPI_CHAR, TRACKER_RANK, TRACKER_REQUEST_TAG, MPI_COMM_WORLD);

    return NULL;
}

// function to be executed by upload thread
void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    while (1) {    
        MPI_Status status;
        char request[15] = "";

        // receive request type from peer or tracker
        MPI_Recv(request, 15, MPI_CHAR, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status);

        // if request is CHUNK REQ, send chunk hash to peer
        if (strcmp(request, "CHUNK REQ") == 0) {
            char filename[MAX_FILENAME];
            int chunk_index = -1;

            // receive filename
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receive chunk index
            MPI_Recv(&chunk_index, 1, MPI_INT, status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            printf("CHUNK REQ from peer %d for file %s chunk %d\n", status.MPI_SOURCE, filename, chunk_index);

            for(int i = 0; i < files_owned_count; i++) {
                if (strcmp(files_owned[i].filename, filename) == 0) {
                    // send chunk hash to peer
                    MPI_Send(files_owned[i].chunks[chunk_index].hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, CHUNK_TAG, MPI_COMM_WORLD);
                }
            }
        }

        // if request is DONE, exit thread
        if (strcmp(request, "DONE") == 0) {
            printf("DONE from peer %d\n", rank);
            return NULL;
        }
    }
}

// function to receive files information from peers and add them to tracker database
void add_files_to_tracker_database(int numtasks, int rank) {
    int files_owned, chunks;
    char filename[MAX_FILENAME];
    char hash[HASH_SIZE];

    for(int i = 1; i < numtasks; i++) {
        // receive number of files owned by peer
        MPI_Recv(&files_owned, 1, MPI_INT, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for(int j = 0; j < files_owned; j++) {
            // receive filename
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receive number of chunks
            MPI_Recv(&chunks, 1, MPI_INT, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // complete file structure
            files[files_count].seed_count = 0;
            files[files_count].seed[files[files_count].seed_count++] = i;
            strcpy(files[files_count].filename, filename);
            files[files_count].chunks_count = 0;

            for(int k = 0; k < chunks; k++) {
                // receive chunk hash
                MPI_Recv(hash, HASH_SIZE, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // add chunk to file structure
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

    // send confirmation to peers
    for(int i = 1; i < numtasks; i++) {
        char response[10] = "FILES OK";
        MPI_Send(response, 10, MPI_CHAR, i, FILE_INFO_TAG, MPI_COMM_WORLD);
    }
}

// function to send DONE message to all peers
void send_done_message(int numtasks) {
    char response[10] = "DONE";
    for(int i = 1; i < numtasks; i++) {
        MPI_Send(response, 10, MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD);
    }

    printf("DONE sent to all peers\n");
}

void tracker(int numtasks, int rank) {
    // receive files information from peers and add them to tracker database
    add_files_to_tracker_database(numtasks, rank);

    // wait for all peers to be ready
    int clients_ready[numtasks];
    for(int i = 0; i < numtasks; i++) {
        clients_ready[i] = 0;
    }

    while(1) {
        char request[15];
        MPI_Status status;

        // receive request type from peer
        MPI_Recv(request, 15, MPI_CHAR, MPI_ANY_SOURCE, TRACKER_REQUEST_TAG, MPI_COMM_WORLD, &status);

        // if request is OWNERS REQ, send files information to peer
        if (strcmp(request, "OWNERS REQ") == 0) {
            // receive filename
            char filename_req[MAX_FILENAME];
            MPI_Recv(filename_req, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, TRACKER_REQUEST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            printf("OWNERS REQ from peer %d for file %s\n", status.MPI_SOURCE, filename_req);

            for(int i = 0; i < files_count; i++) {
                if (strcmp(files[i].filename, filename_req) == 0) {
                    // send number of chunks to peer
                    MPI_Send(&files[i].chunks_count, 1, MPI_INT, status.MPI_SOURCE, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD);
                    
                    // send chunks information to peer
                    for(int j = 0; j < files[i].chunks_count; j++) {
                        // send number of owners to peer
                        MPI_Send(&files[i].chunks[j].owners_count, 1, MPI_INT, status.MPI_SOURCE, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD);
                        // send chunk index to peer
                        MPI_Send(&files[i].chunks[j].index, 1, MPI_INT, status.MPI_SOURCE, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD);
                        // send owners to peer
                        MPI_Send(files[i].chunks[j].owners, files[i].chunks[j].owners_count, MPI_INT, status.MPI_SOURCE, TRACKER_RESPONSE_TAG, MPI_COMM_WORLD);
                    }

                    break;
                }
            }
        }

        // if request is READY, mark peer as ready
        if(strcmp(request, "READY") == 0) {
            clients_ready[status.MPI_SOURCE] = 1;
            printf("Peer %d is ready\n", status.MPI_SOURCE);

            // check if all peers are ready
            int all_ready = 1;
            for(int i = 1; i < numtasks; i++) {
                if(clients_ready[i] == 0) {
                    all_ready = 0;
                    break;
                }
            }

            // if all peers are ready, send DONE message to all peers
            if(all_ready == 1) {            
                send_done_message(numtasks);
                return;
            }

        }
    }
}

void peer(int numtasks, int rank) {
    numtasks = numtasks;

    // compute input file name
    char infilename[MAX_FILENAME] = "in";
    char rank_str[5];
    snprintf(rank_str, sizeof(int), "%d", rank);
    strcat(infilename, rank_str);
    strcat(infilename, ".txt");

    FILE *input = fopen(infilename, "r");

    if (input == NULL) {
        printf("Eroare la deschiderea fisierului de input\n");
        exit(-1);
    }

    int chunks;
    char filename[MAX_FILENAME], hash[HASH_SIZE];

    // read files owned number from input file
    fscanf(input, "%d", &files_owned_count);

    // send files owned number to tracker
    MPI_Send(&files_owned_count, 1, MPI_INT, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

    for(int i = 0; i < files_owned_count; i++) {
        // read file name from input file
        fscanf(input, "%s", filename);
        strcpy(files_owned[i].filename, filename);

        // send file name to tracker
        MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

        // read chunks number from input file
        fscanf(input, "%d", &chunks);
        files_owned[i].chunks_count = chunks;

        // send chunks number to tracker
        MPI_Send(&chunks, 1, MPI_INT, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);

        for(int j = 0; j < chunks; j++) {
            // read chunk hash from input file
            fscanf(input, "%s", hash);
            hash[HASH_SIZE] = '\0';
            strcpy(files_owned[i].chunks[j].hash, hash);

            // send chunk hash to tracker
            MPI_Send(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, FILE_INFO_TAG, MPI_COMM_WORLD);
        }
    }

    // read files requested number from input file
    fscanf(input, "%d", &files_requested_count);

    for(int i = 0; i < files_requested_count; i++) {
        // read file name from input file
        fscanf(input, "%s", filename);
        strcpy(files_requested[i].filename, filename);

        for(int j = 0; j < MAX_CHUNKS; j++) {
            files_requested[i].chunks_recv[j] = -1;
        }
    }

    fclose(input);

    // receive confirmation from tracker
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
