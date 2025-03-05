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
#define MAX_PEERS 100

typedef struct {
    char *filename;
    int num_segments;
    char **segments;
} File;

typedef struct {
    int num_files;
    File files[MAX_FILES];
    int num_wanted_files;
    char (*wanted_files)[MAX_FILENAME];
    int rank;
    int terminate_flag;
} ClientData;

typedef struct {
    File file;
    int num_peers;
    int peers[MAX_PEERS];
} Swarm;

typedef struct {
    int num_files;
    Swarm swarms[MAX_FILES];
    int num_completed_clients;
} TrackerData;

TrackerData tracker_data;

void read_input_file(int rank, ClientData *client_data) {
    char filename[20];
    sprintf(filename, "in%d.txt", rank);
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    fscanf(file, "%d", &client_data->num_files);

    for (int i = 0; i < client_data->num_files; i++) {
        client_data->files[i].filename = (char *)malloc(
            MAX_FILENAME * sizeof(char));
        fscanf(file, "%s %d", client_data->files[i].filename,
               &client_data->files[i].num_segments);
        client_data->files[i].segments = (char **)malloc(
            client_data->files[i].num_segments * sizeof(char *));
        for (int j = 0; j < client_data->files[i].num_segments; j++) {
            client_data->files[i].segments[j] = (char *)malloc(
                (HASH_SIZE + 1) * sizeof(char));
            fscanf(file, "%s", client_data->files[i].segments[j]);
            client_data->files[i].segments[j][HASH_SIZE] = '\0';
        }
    }

    fscanf(file, "%d", &client_data->num_wanted_files);
    client_data->wanted_files = (char (*)[MAX_FILENAME])malloc(
        client_data->num_wanted_files * MAX_FILENAME * sizeof(char));
    for (int i = 0; i < client_data->num_wanted_files; i++) {
        fscanf(file, "%s", client_data->wanted_files[i]);
    }

    fclose(file);
}

void *download_thread_func(void *arg) {
    ClientData *client_data = (ClientData *) arg;
    int rank = client_data->rank;
    MPI_Status status;
    int num_peers;
    int peers[MAX_PEERS];
    char segment_request[HASH_SIZE + 1];
    char segment_response[4];
    int num_segments;
    char **segments;

    // Request segments from peers/seeds
    for (int i = 0; i < client_data->num_wanted_files; i++) {
        // Request segment information from tracker
        printf("Client %d: Requesting segment information for file %s from "
               "tracker\n", rank, client_data->wanted_files[i]);
        MPI_Send(client_data->wanted_files[i], MAX_FILENAME, MPI_CHAR,
                 TRACKER_RANK, 1, MPI_COMM_WORLD);
        MPI_Recv(&num_segments, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD,
                 &status);
        printf("Client %d: Received %d segments for file %s\n", rank,
               num_segments, client_data->wanted_files[i]);

        // Allocate memory for segments
        segments = (char **)malloc(num_segments * sizeof(char *));
        for (int j = 0; j < num_segments; j++) {
            segments[j] = (char *)malloc((HASH_SIZE + 1) * sizeof(char));
            MPI_Recv(segments[j], HASH_SIZE, MPI_CHAR, TRACKER_RANK, 1,
                     MPI_COMM_WORLD, &status);
            segments[j][HASH_SIZE] = '\0'; // Ensure null termination
        }

        client_data->files[client_data->num_files].filename = (char *)malloc(
            MAX_FILENAME * sizeof(char));
        strcpy(client_data->files[client_data->num_files].filename,
               client_data->wanted_files[i]);
        client_data->files[client_data->num_files].segments = (char **)malloc(
            num_segments * sizeof(char *));
        client_data->files[client_data->num_files].num_segments = 0;

        int segments_downloaded = 0;
        int last_peer_index = -1;
        for (int j = 0; j < num_segments; j++) {
            // Request list of peers from tracker
            if (segments_downloaded % 10 == 0) {
                printf("Client %d: Requesting list of peers for segment %d of "
                       "file %s from tracker\n", rank, j,
                       client_data->wanted_files[i]);
                MPI_Send(client_data->wanted_files[i], MAX_FILENAME, MPI_CHAR,
                         TRACKER_RANK, 2, MPI_COMM_WORLD);
                MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, 2, MPI_COMM_WORLD,
                         &status);
                MPI_Recv(peers, num_peers, MPI_INT, TRACKER_RANK, 2,
                         MPI_COMM_WORLD, &status);
                printf("Client %d: Received %d peers for segment %d of file "
                       "%s\n", rank, num_peers, j, client_data->wanted_files[i]);
            }

            int segment_found = 0;
            int attempts = 0;
            // Try to download the segment from all peers until the segment is
            // found or all peers are tried
            while (!segment_found && attempts < num_peers) {
                // Find the next peer in a round-robin manner
                last_peer_index = (last_peer_index + 1) % num_peers;
                int peer = peers[last_peer_index];

                printf("Client %d: Requesting segment %d of file %s from peer "
                       "%d\n", rank, j, client_data->wanted_files[i], peer);
                strcpy(segment_request, segments[j]);
                MPI_Send(segment_request, HASH_SIZE, MPI_CHAR, peer, 3,
                         MPI_COMM_WORLD);
                MPI_Recv(segment_response, 4, MPI_CHAR, peer, 0, MPI_COMM_WORLD,
                         &status);
                segment_response[3] = '\0';

                if (strcmp(segment_response, "ACK") == 0) {
                    // Save segment locally
                    client_data->files[client_data->num_files].segments[j] =
                        (char *)malloc((HASH_SIZE + 1) * sizeof(char));
                    strcpy(client_data->files[client_data->num_files].segments[j],
                           segments[j]);
                    printf("Client %d: Received segment %d of file %s from peer "
                           "%d\n", rank, j, client_data->wanted_files[i], peer);
                    segments_downloaded++;
                    segment_found = 1;
                    client_data->files[client_data->num_files].num_segments++;
                } else {
                    printf("Client %d: Peer %d did not have segment %d of file "
                           "%s\n", rank, peer, j, client_data->wanted_files[i]);
                }
                attempts++;
            }

            if (!segment_found) {
                printf("Client %d: Could not find segment %d of file %s from "
                       "any peer\n", rank, j, client_data->wanted_files[i]);
            }
        }

        // Update number of files downloaded
        if (client_data->files[client_data->num_files].num_segments ==
            num_segments) {
            // Print segments to output file
            char output_filename[30];
            sprintf(output_filename, "client%d_%s", rank,
                    client_data->wanted_files[i]);
            FILE *output_file = fopen(output_filename, "w");
            if (output_file) {
                for (int j = 0; j < num_segments; j++) {
                    fprintf(output_file, "%s\n",
                            client_data->files[client_data->num_files].segments[j]);
                }
                fclose(output_file);
            } else {
                printf("Client %d: Error opening output file %s\n", rank,
                       output_filename);
            }
            client_data->num_files++;
        }

        // Free dynamically allocated segments
        for (int j = 0; j < num_segments; j++) {
            free(segments[j]);
        }
        free(segments);
    }

    // Inform tracker that all files are downloaded
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, 5, MPI_COMM_WORLD);
    printf("Client %d: Informed tracker that all files are downloaded\n", rank);

    return NULL;
}

void *upload_thread_func(void *arg) {
    ClientData *client_data = (ClientData *) arg;
    int rank = client_data->rank;
    MPI_Status status;
    char segment_request[HASH_SIZE + 1];
    char ack[4] = "ACK";
    char nok[4] = "NOK";
    MPI_Request request;
    int flag;

    while (!client_data->terminate_flag) {
        // Receive segment request from peer
        MPI_Irecv(segment_request, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 3,
                  MPI_COMM_WORLD, &request);

        // Wait for the request to complete or check the terminate flag
        while (1) {
            // Check if the request is complete
            MPI_Test(&request, &flag, &status);
            if (flag) {
                segment_request[HASH_SIZE] = '\0';

                // Check if the segment is available
                int found = 0;
                for (int i = 0; i < client_data->num_files; i++) {
                    for (int j = 0; j < client_data->files[i].num_segments; j++) {
                        if (strcmp(client_data->files[i].segments[j],
                                   segment_request) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (found) break;
                }

                // Send response to peer
                if (found) {
                    MPI_Send(ack, 4, MPI_CHAR, status.MPI_SOURCE, 0,
                             MPI_COMM_WORLD);
                } else {
                    MPI_Send(nok, 4, MPI_CHAR, status.MPI_SOURCE, 0,
                             MPI_COMM_WORLD);
                }
                break;
            }

            // Check the terminate flag
            if (client_data->terminate_flag) {
                MPI_Cancel(&request);
                break;
            }
        }
    }

    printf("Client %d: Upload thread terminating\n", rank);
    return NULL;
}


void tracker(int numtasks, int rank) {
    MPI_Status status;
    int num_files;
    char filename[MAX_FILENAME];
    int num_segments;
    char segment[HASH_SIZE + 1];

    tracker_data.num_files = 0;
    tracker_data.num_completed_clients = 0;

    for (int i = 1; i < numtasks; i++) {
        MPI_Recv(&num_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
        for (int j = 0; j < num_files; j++) {
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD,
                     &status);
            MPI_Recv(&num_segments, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

            // Add client to the swarm for this file
            tracker_data.swarms[tracker_data.num_files].file.filename =
                (char *)malloc((strlen(filename) + 1) * sizeof(char));
            strcpy(tracker_data.swarms[tracker_data.num_files].file.filename,
                   filename);
            tracker_data.swarms[tracker_data.num_files].file.num_segments =
                num_segments;
            tracker_data.swarms[tracker_data.num_files].file.segments =
                (char **)malloc(num_segments * sizeof(char *));
            
            for (int k = 0; k < num_segments; k++) {
                MPI_Recv(segment, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD,
                         &status);
                segment[HASH_SIZE] = '\0';
                tracker_data.swarms[tracker_data.num_files].file.segments[k] =
                    (char *)malloc((HASH_SIZE + 1) * sizeof(char));
                strcpy(tracker_data.swarms[tracker_data.num_files].file.segments[k],
                       segment);
            }
            
            tracker_data.swarms[tracker_data.num_files].peers[0] = i;
            tracker_data.swarms[tracker_data.num_files].num_peers = 1;
            tracker_data.num_files++;
        }
    }

    // Send ACK to all clients
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("ACK", 3, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }

    while (tracker_data.num_completed_clients < numtasks - 1) {
        char requested_filename[MAX_FILENAME];
        MPI_Recv(requested_filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE,
                 MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == 1) {
            // Respond with number of segments and segment hashes
            for (int k = 0; k < tracker_data.num_files; k++) {
                if (strcmp(tracker_data.swarms[k].file.filename,
                           requested_filename) == 0) {
                    MPI_Send(&tracker_data.swarms[k].file.num_segments, 1,
                             MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
                    for (int j = 0; j < tracker_data.swarms[k].file.num_segments;
                         j++) {
                        MPI_Send(tracker_data.swarms[k].file.segments[j],
                                 HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 1,
                                 MPI_COMM_WORLD);
                    }
                    // Mark client as peer for this file
                    tracker_data.swarms[k].peers[
                        tracker_data.swarms[k].num_peers++] = status.MPI_SOURCE;
                    break;
                }
            }
        } else if (status.MPI_TAG == 2) {
            // Respond with list of peers
            for (int k = 0; k < tracker_data.num_files; k++) {
                if (strcmp(tracker_data.swarms[k].file.filename,
                           requested_filename) == 0) {
                    MPI_Send(&tracker_data.swarms[k].num_peers, 1, MPI_INT,
                             status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                    MPI_Send(tracker_data.swarms[k].peers,
                             tracker_data.swarms[k].num_peers, MPI_INT,
                             status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                    break;
                }
            }
        } else if (status.MPI_TAG == 5) {
            // All files downloaded, mark client as completed
            tracker_data.num_completed_clients++;
            printf("Tracker: Client %d completed downloading all files\n",
                   status.MPI_SOURCE);
        }
    }

    // Send termination signal to all clients
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("TERMINATE", 10, MPI_CHAR, i, 6, MPI_COMM_WORLD);
        printf("Tracker: Sent termination signal to client %d\n", i);
    }

    // Free dynamically allocated memory in tracker_data
    for (int i = 0; i < tracker_data.num_files; i++) {
        free(tracker_data.swarms[i].file.filename);
        for (int j = 0; j < tracker_data.swarms[i].file.num_segments; j++) {
            free(tracker_data.swarms[i].file.segments[j]);
        }
        free(tracker_data.swarms[i].file.segments);
    }
    printf("Tracker: Tracker thread terminating\n");

}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;
    MPI_Status mpi_status;
    char ack[4];
    char terminate_msg[10];

    ClientData *client_data = (ClientData *) malloc(sizeof(ClientData));
    client_data->rank = rank;
    client_data->terminate_flag = 0;

    read_input_file(rank, client_data);

    // Inform tracker about the files this peer has
    printf("Client %d: Informing tracker about files\n", rank);
    MPI_Send(&client_data->num_files, 1, MPI_INT, TRACKER_RANK, 0,
             MPI_COMM_WORLD);
    for (int i = 0; i < client_data->num_files; i++) {
        MPI_Send(client_data->files[i].filename, MAX_FILENAME, MPI_CHAR,
                 TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&client_data->files[i].num_segments, 1, MPI_INT, TRACKER_RANK,
                 0, MPI_COMM_WORLD);
        for (int j = 0; j < client_data->files[i].num_segments; j++) {
            MPI_Send(client_data->files[i].segments[j], HASH_SIZE, MPI_CHAR,
                     TRACKER_RANK, 0, MPI_COMM_WORLD);
        }
    }

    // Wait for ACK from tracker
    MPI_Recv(ack, 3, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
    printf("Client %d: Received ACK from tracker\n", rank);

    // Create download and upload threads
    r = pthread_create(&download_thread, NULL, download_thread_func,
                       (void *) client_data);
    if (r) {
        printf("Client %d: Error creating download thread\n", rank);
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func,
                       (void *) client_data);
    if (r) {
        printf("Client %d: Error creating upload thread\n", rank);
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Client %d: Error waiting for download thread\n", rank);
        exit(-1);
    }

    // Wait for termination signal from tracker
    MPI_Recv(terminate_msg, 10, MPI_CHAR, TRACKER_RANK, 6, MPI_COMM_WORLD,
             &mpi_status);
    printf("Client %d: Received termination signal from tracker\n", rank);

    // Signal the upload thread to terminate
    client_data->terminate_flag = 1;

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Client %d: Error waiting for upload thread\n", rank);
        exit(-1);
    }

    printf("Client %d: Peer thread terminating\n", rank);

    // Free dynamically allocated memory in client_data
    for (int i = 0; i < client_data->num_files; i++) {
        free(client_data->files[i].filename);
        for (int j = 0; j < client_data->files[i].num_segments; j++) {
            free(client_data->files[i].segments[j]);
        }
        free(client_data->files[i].segments);
    }
    free(client_data->wanted_files);
    free(client_data);
}

int main (int argc, char *argv[]) {
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI does not support multiple threads\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    printf("Rank %d: Terminating\n", rank);

    MPI_Finalize();
}