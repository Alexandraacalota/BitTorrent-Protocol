#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define UPDATE_LIMIT 10  // send update every 10 segments
#define SWARM_REQUEST 3  // tag for swarm request from client to tracker
#define FINISHED 10  // tag for messages from clients before closing download thread
#define UPDATE 5  // tag for update messages every UPDATE_LIMIT segments
#define SEED 6  // tag for message sent from client that finished downloading a file
#define HASH 7  // tag for message sent to the upload thread
#define ACK 8  // tag for message send from the upload thread

typedef struct {
	int rank;
	char** hashes;
	int hashes_number;
	int seed;
} client;

typedef struct {
	client* clients;
	int filesize;
	char* filename;
} swarm;

typedef struct {
	swarm* swarms;
	int swarms_number;
} tracker_struct;

typedef struct {
	char name_and_size[MAX_FILES][HASH_SIZE + 2];
	char files[MAX_FILES][MAX_CHUNKS][HASH_SIZE + 2];
	int rank;
	int sizes[MAX_FILES];
	char wanted_files[MAX_FILES][MAX_FILENAME];
	int nr_wanted_files;
	int files_count;
	int numtasks;
} thread_func_argument;

void *download_thread_func(void *arg)
{
	thread_func_argument* argument = (thread_func_argument*) arg;
	MPI_Status status;

	// send initial owned files names, sizes and hashes to the tracker
	MPI_Send(argument->name_and_size, MAX_FILES * (HASH_SIZE + 2), MPI_CHAR,
			 TRACKER_RANK, 0, MPI_COMM_WORLD);
	MPI_Send(argument->files, MAX_FILES * MAX_CHUNKS * (HASH_SIZE + 2), MPI_CHAR,
			 TRACKER_RANK, 1, MPI_COMM_WORLD);

	char ack[4];

	// receive ACK message from tracker to proceed with the downloads
	MPI_Recv(&ack, 4, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	// for each wanted file
	for (int i = 0; i < argument->nr_wanted_files; i++) {
		int segment_index = 0;

		// start from a random client
		int client_nr = rand() % (argument->numtasks - 1);
		int filesize;

		int ok = 1;
		int start_download = 1;

		// cat timp segment_index < filesize
		while (ok == 1) {

			// allocate memory for the array of clients it can download
			// segments from
			client* clients = (client*)calloc(argument->numtasks - 1, sizeof(client));

			for (int j = 0; j < argument->numtasks - 1; j++) {
				clients[j].hashes = (char**)calloc(MAX_CHUNKS, sizeof(char*));
				clients[j].rank = 0;
				clients[j].seed = 0;
				clients[j].hashes_number = 0;
				for (int k = 0; k < MAX_CHUNKS; k++) {
					clients[j].hashes[k] = (char*)calloc((HASH_SIZE + 2), sizeof(char));
				}
			}

			int nr_seg_for_update = 0;

			// send file name request to the tracker for its swarm
			MPI_Send(argument->wanted_files[i], MAX_FILENAME, MPI_CHAR, TRACKER_RANK,
					 SWARM_REQUEST, MPI_COMM_WORLD);

			// receives the swarm's data from the tracker
			MPI_Recv(&filesize, 1, MPI_INT, TRACKER_RANK, MPI_ANY_TAG, MPI_COMM_WORLD,
					 &status);

			int msg_tag = status.MPI_TAG;

			for (int j = 0; j < argument->numtasks - 1; j++) {

				// receive client's rank
				MPI_Recv(&clients[j].rank, 1, MPI_INT, TRACKER_RANK, msg_tag,
						 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

				// receive client's hashes_number
				MPI_Recv(&clients[j].hashes_number, 1, MPI_INT, TRACKER_RANK, msg_tag,
						 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

				// receive client's hashes
				for (int k = 0; k < MAX_CHUNKS; k++) {
					MPI_Recv(clients[j].hashes[k], HASH_SIZE + 2, MPI_CHAR,
							 TRACKER_RANK, msg_tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				}
			}

			// if the file has just started downloading, save it the name_and_size
			// array the name and size of the file
			if (start_download == 1) {
				sprintf(argument->name_and_size[argument->files_count], "%s %d",
						argument->wanted_files[i], filesize);
				argument->files_count++;
				start_download = 0;
			}

			// while it hasn't downloaded all segments of the file
			while (segment_index != filesize) {

				// check if client with rank client_nr + 1 has the wanted segment
				if (clients[client_nr].hashes[segment_index][0] != '\0') {

					// send segment request
					MPI_Send(clients[client_nr].hashes[segment_index], HASH_SIZE + 2,
							 MPI_CHAR, clients[client_nr].rank, HASH, MPI_COMM_WORLD);

					char ack[4];

					// receive segment ACK
					MPI_Recv(&ack, 4, MPI_CHAR, clients[client_nr].rank, ACK,
							 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

					nr_seg_for_update++;

					// save the segment in the current client's files
					strcpy(argument->files[argument->files_count - 1][segment_index],
						   clients[client_nr].hashes[segment_index]);
					segment_index++;
				}

				// try to download from another client the following segment
				client_nr++;
				if (client_nr >= argument->numtasks - 1) {
					client_nr = 0;
				}

				// if the file has finished downloading
				if (segment_index == filesize) {

					// send file name
					MPI_Send(argument->wanted_files[i], MAX_FILENAME, MPI_CHAR,
							 TRACKER_RANK, SEED, MPI_COMM_WORLD);
					
					// send segments of the file
					for (int j = 0; j < MAX_CHUNKS; j++) {
						MPI_Send(argument->files[argument->files_count - 1][j],
								 HASH_SIZE + 2, MPI_CHAR, TRACKER_RANK, SEED,
								 MPI_COMM_WORLD);
						ok = 0;
					}
					break;
				}

				// if the client has downloaded UPDATE_LIMIT segments since the
				// last update sent to the tracker, send another one
				if (nr_seg_for_update == UPDATE_LIMIT) {
					MPI_Send(argument->wanted_files[i], MAX_FILENAME, MPI_CHAR,
							 TRACKER_RANK, UPDATE, MPI_COMM_WORLD);
					for (int j = 0; j < MAX_CHUNKS; j++) {
						MPI_Send(argument->files[argument->files_count - 1][j],
								 HASH_SIZE + 2, MPI_CHAR, TRACKER_RANK, UPDATE,
								 MPI_COMM_WORLD);
					}
					break;
				}
			}

			// free allocated memory for the clients list
			for (int j = 0; j < argument->numtasks - 1; j++) {
				for (int k = 0; k < MAX_CHUNKS; k++) {
					free(clients[j].hashes[k]);
				}
				free(clients[j].hashes);
			}
			free(clients);
		}


		char out_filename[2 * MAX_FILENAME];
		FILE *f;

		// create the name of the output file
		sprintf(out_filename, "client%d_%s", argument->rank,
				argument->wanted_files[i]);

		f = fopen(out_filename, "w");
		
		for (int j = 0; j < filesize; j++) {
			if (j == filesize - 1) {
				fprintf(f, "%s", argument->files[argument->files_count - 1][j]);
			} else {
				fprintf(f, "%s\n", argument->files[argument->files_count - 1][j]);
			}
		}
		fclose(f);

	}

	// after finishing downloading all wanted files, notify the tracker
	char ok[MAX_FILENAME] = "OK";

	MPI_Send(&ok, MAX_FILENAME, MPI_CHAR, TRACKER_RANK , FINISHED, MPI_COMM_WORLD);

	// close download thread
	return NULL;
}

void *upload_thread_func(void *arg)
{
	char msg[HASH_SIZE + 2];
	MPI_Status status;
	int ok = 1;

	while (ok == 1) {

		// seed/ peer receives message
		MPI_Recv(msg, HASH_SIZE + 2, MPI_CHAR, MPI_ANY_SOURCE, HASH,
				 MPI_COMM_WORLD, &status);
		int src = status.MPI_SOURCE;

		// if it is from the tracker, end thread
		if (src == 0) {
			ok = 0;

		// if it is from another client, gives him the segment
		// through "ACK" message
		} else {
			char ack[4] = "ACK";

			MPI_Send(&ack, 4, MPI_CHAR, src, ACK, MPI_COMM_WORLD);
		}
	}
	return NULL;
}

void tracker(int numtasks, int rank) {
	char recv_files[MAX_FILES][MAX_CHUNKS][HASH_SIZE + 2];
	MPI_Status status;
	tracker_struct info;
	int finished = 0;

	// allocate memory inside the info variable, that will contain
	// the swarms list
	info.swarms_number = 0;
	info.swarms = (swarm*)calloc(MAX_FILES, sizeof(swarm));

	for (int i = 0; i < MAX_FILES; i++) {
		info.swarms[i].filesize = 0;
		info.swarms[i].clients = (client*)calloc(numtasks - 1, sizeof(client));
		info.swarms[i].filename = (char*)calloc(MAX_FILENAME, sizeof(char));

		for (int j = 0; j < numtasks - 1; j++) {
			info.swarms[i].clients[j].hashes = (char **)calloc(MAX_CHUNKS,
															   sizeof(char *));

			for (int k = 0; k < MAX_CHUNKS; k++) {
				info.swarms[i].clients[j].rank = 0;
				info.swarms[i].clients[j].seed = 0;
				info.swarms[i].clients[j].hashes_number = 0;
				info.swarms[i].clients[j].hashes[k] = (char *)calloc((HASH_SIZE + 2),
																	 sizeof(char));
			}
		}
	}

	// for each client, tracker receives initial files
	for (int i = 1; i < numtasks; i++) {

		char file_info[MAX_FILES][HASH_SIZE + 2];

		memset(file_info, 0, MAX_FILES * (HASH_SIZE + 2));
		
		// tracker receives the client's array of files
		// information (names and sizes)
		MPI_Recv(file_info, MAX_FILES * (HASH_SIZE + 2), MPI_CHAR, i, 0,
				 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		int size;
		char name[MAX_FILENAME];

		memset(name, 0, MAX_FILENAME);

		// tracker receives the client's files and the segments for
		// each of them in a 3d array
		MPI_Recv(recv_files, MAX_FILES * MAX_CHUNKS * (HASH_SIZE + 2), MPI_CHAR,
				 i, 1, MPI_COMM_WORLD, &status);

		int src = status.MPI_SOURCE;

		// for each file of the client
		for (int j = 0; j < MAX_FILES; j++) {
			if (file_info[j][0] != '\0') {

				// get the name and size of the file in two variables
				sscanf(file_info[j], "%s %d", name, &size);
				int place_in_swarm = -1;

				// look through all the files added to the swarms list
				for (int k = 0; k < info.swarms_number; k++) {

					// check if the name already exists in the swarms list
					if (strcmp(info.swarms[k].filename, name) == 0) {

						// the client's file segments are being placed in
						// an already existing swarm
						place_in_swarm = k;

						// copy received data to the swarm
						info.swarms[place_in_swarm].clients[src - 1].rank = src;
						info.swarms[place_in_swarm].clients[src - 1].seed = 1;
						info.swarms[place_in_swarm].clients[src - 1].hashes_number = size;

						for (int l = 0; l < MAX_CHUNKS; l++) {

							// copy received hashes to info.swarms of the current
							// file and client
							strcpy(info.swarms[place_in_swarm].clients[src - 1].hashes[l],
								   recv_files[j][l]);
						}
						break;
					}
				}

				// if there is not yet a swarm for the file
				if (place_in_swarm == -1) {
					place_in_swarm = info.swarms_number;

					// save its name and size to the swarm
					strcpy(info.swarms[place_in_swarm].filename, name);
					info.swarms[place_in_swarm].filesize = size;

					// increment the number of swarms in the tracker's list
					info.swarms_number++;

					// copy received data to the swarm
					info.swarms[place_in_swarm].clients[src - 1].rank = src;
					info.swarms[place_in_swarm].clients[src - 1].seed = 1;
					info.swarms[place_in_swarm].clients[src - 1].hashes_number = size;
					// for each segment in file
					for (int l = 0; l < MAX_CHUNKS; l++) {

						// copy received hashes from the client to the file's swarm
						strcpy(info.swarms[place_in_swarm].clients[src - 1].hashes[l],
							   recv_files[j][l]);
					}
				}
			}
		}
	}

	// send ACK messages to all clients to let them know that they can
	// start asking for files to download
	for (int i = 1; i < numtasks; i++) {
		char ack[4] = "ACK";

		MPI_Send(&ack, 4, MPI_CHAR, i, 0, MPI_COMM_WORLD);
	}

	// while there are still clients that have not finished all downloads,
	// let them keep their upload threads up
	while (finished != numtasks - 1) {
		char filename[MAX_FILENAME];

		// receive a message from a client
		MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG,
				 MPI_COMM_WORLD, &status);
		int src = status.MPI_SOURCE;
		int tag = status.MPI_TAG;

		// if the message is from a client who requests a file swarm
		if (tag == SWARM_REQUEST) {

			// search through the swarms list to find the name
			// of the file requested by the client
			for (int i = 0; i < info.swarms_number; i++) {
				if (strcmp(info.swarms[i].filename, filename) == 0) {
					int src = status.MPI_SOURCE;
					int msg_tag = status.MPI_TAG;

					// send swarm data to the client
					MPI_Send(&info.swarms[i].filesize, 1, MPI_INT, src, msg_tag,
							 MPI_COMM_WORLD);
					for (int j = 0; j < numtasks - 1; j++) {
						MPI_Send(&info.swarms[i].clients[j].rank, 1, MPI_INT, src,
								 msg_tag, MPI_COMM_WORLD);
						MPI_Send(&info.swarms[i].clients[j].hashes_number, 1,
								 MPI_INT, src, msg_tag, MPI_COMM_WORLD);
						for (int k = 0; k < MAX_CHUNKS; k++) {
							MPI_Send(info.swarms[i].clients[j].hashes[k], HASH_SIZE + 2,
									 MPI_CHAR, src, msg_tag, MPI_COMM_WORLD);
						}
					}
					break;
				}
			}

		// if a client has finished downloading all wanted files
		} else if (tag == FINISHED) {

			// increment the number of clients that have finished downloading
			finished++;

		// if the message is with an UPDATE_LIMIT segments update from a client
		} else if (tag == UPDATE) {

			// look for the file that the client sent an update on
			for (int i = 0; i < MAX_FILES; i++) {
				if (strcmp(filename, info.swarms[i].filename) == 0) {
					for (int j = 0; j < MAX_CHUNKS; j++) {
						memset(info.swarms[i].clients[src - 1].hashes[j], 0,
							   HASH_SIZE + 2);
						char hash[HASH_SIZE + 2];

						// receive the hashes that the client now owns
						// and put them in the swarm
						MPI_Recv(&hash, HASH_SIZE + 2, MPI_CHAR, src, tag,
								 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						strcpy(info.swarms[i].clients[src - 1].hashes[j], hash);
					}
					info.swarms[i].clients[src - 1].hashes_number += UPDATE_LIMIT;
					info.swarms[i].clients[src - 1].rank = src;
					
					// restarting downloading process, the client sends again the
					// name of the client that he wants and the tracker receives it
					// and sends back the current swarm of that file
					MPI_Recv(&filename, MAX_FILENAME, MPI_CHAR, src, SWARM_REQUEST,
							 MPI_COMM_WORLD, &status);
					int msg_tag = status.MPI_TAG;

					MPI_Send(&info.swarms[i].filesize, 1, MPI_INT, src, msg_tag,
							 MPI_COMM_WORLD);
					for (int j = 0; j < numtasks - 1; j++) {
						MPI_Send(&info.swarms[i].clients[j].rank, 1, MPI_INT, src,
								 msg_tag, MPI_COMM_WORLD);
						MPI_Send(&info.swarms[i].clients[j].hashes_number, 1,
								 MPI_INT, src, msg_tag, MPI_COMM_WORLD);

						for (int k = 0; k < MAX_CHUNKS; k++) {
							MPI_Send(info.swarms[i].clients[j].hashes[k], HASH_SIZE + 2,
									 MPI_CHAR, src, msg_tag, MPI_COMM_WORLD);
						}
					}
					break;
				}
			}

		// receives the final update from a client for a file that has
		// finished downloading
		} else if (tag == SEED) {

			// fint the file in the swarm and copy the data received from
			// the client to its swarm in tracker's info variable
			for (int i = 0; i < MAX_FILES; i++) {
				if (strcmp(filename, info.swarms[i].filename) == 0) {
					for (int j = 0; j < MAX_CHUNKS; j++) {
						memset(info.swarms[i].clients[src - 1].hashes[j],
							   0, HASH_SIZE + 2);
						char hash[HASH_SIZE + 2];

						MPI_Recv(&hash, HASH_SIZE + 2, MPI_CHAR, src, MPI_ANY_TAG,
								 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						strcpy(info.swarms[i].clients[src - 1].hashes[j], hash);
					}
					info.swarms[i].clients[src - 1].hashes_number =
															info.swarms[i].filesize;
				}
			}
		}
	}

	char msg[HASH_SIZE + 2];

	// after all clients have finished downloading, send shutdown message
	// to make them close upload thread as well
	strcpy(msg, "shutdown");
	for (int i = 1; i < numtasks; i++) {
		MPI_Send(&msg, HASH_SIZE + 2, MPI_CHAR, i, HASH, MPI_COMM_WORLD);
	}

	// free the memory allocated
	for (int i = 0; i < MAX_FILES; i++) {
		for (int j = 0; j < numtasks - 1; j++) {
			for (int k = 0; k < MAX_CHUNKS; k++) {
				free(info.swarms[i].clients[j].hashes[k]);
			}
		free(info.swarms[i].clients[j].hashes);
	}
		free(info.swarms[i].clients);
		free(info.swarms[i].filename);
	}

	free(info.swarms);
}

void peer(int numtasks, int rank) {
	pthread_t download_thread;
	pthread_t upload_thread;
	void *status;
	int r;

	char filename[MAX_FILENAME];
	FILE *f;
	char str[HASH_SIZE + 2];

	// argument passed to download_thread_func at pthread_create call
	thread_func_argument argument;
	memset(argument.files, 0, sizeof(argument.files));
	memset(argument.sizes, 0, sizeof(argument.sizes));
	memset(argument.wanted_files, 0, sizeof(argument.wanted_files));
	memset(argument.name_and_size, 0, MAX_FILES * (HASH_SIZE + 2));

	argument.nr_wanted_files = -1;
	argument.files_count = 0;
	argument.numtasks = numtasks;
	argument.rank = rank;

	strcpy(filename, "in");
	char c = rank + '0';

	strncat(filename, &c, 1);
	strcat(filename, ".txt");

	// open input file of current client
	f = fopen(filename, "r");

	int ok = 0;
	int counter_wanted = 0; // counter for wanted files
	int nr_hashes = 0; // number of hashes in a file

	while (fgets (str, HASH_SIZE + 2, f) != NULL) {
		if (str[strlen(str) - 1] == '\n') {
			str[strlen(str) - 1] = '\0';

			// after the number of owned files is read, the next line of length 1
			// will represent the number of wanted files
			if (ok == 0) {
				ok = 1;
				continue;
			}

		}

		// this str contains the number of wanted files
		if (strlen(str) == 1 && ok == 1) {
			argument.nr_wanted_files = atoi(str);
			continue;
		}

		// the remaining lines contain the names of the wanted files
		if (argument.nr_wanted_files != -1) {
			strcpy(argument.wanted_files[counter_wanted], str);
			counter_wanted++;
			continue;
		}

		// lines containing spaces => owned file name and size
		if (strchr(str, ' ') != NULL) {
			nr_hashes = 0;
			strcpy(argument.name_and_size[argument.files_count], str);
			argument.files_count++;
			continue;
		}

		// copy the hashes of the file to argument.files[<current_file> - 1]

		strcpy(argument.files[argument.files_count - 1][nr_hashes], str);
		nr_hashes++;
	}
	fclose(f);

	r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &argument);
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