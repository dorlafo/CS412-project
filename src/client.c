#include "../include/grass.h"
#include <netinet/in.h>

#define MAX_IP_LENGTH 16
#define MAX_PARALLEL_SIZE 10

char server_ip[MAX_IP_LENGTH];
FILE* input;
FILE* output;
FILE* outputError;


const size_t nb_params = 13;
const char* correct_commands[] = {"login", "pass", "ping", "ls", "cd", "mkdir", "rm", "grep", "date", "whoami", "w", "logout", "exit"};
const size_t command_params[] = {1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0};

//Struct file for put/get
typedef struct {
    char* filepath;
    int port;
    int size;
} file;

//Queue for parallel/multiple get
file get_file[MAX_PARALLEL_SIZE];
size_t get_index_send = 0;
size_t get_index_receive = 0;

//Queue for parallel/multiple put
file put_file[MAX_PARALLEL_SIZE];
size_t put_index_send = 0;
size_t put_index_receive = 0;

pthread_t thread_id;

// Upload a file to the server
void* send_file(void* in) {
    file* f = (file*) in;

    int sock = get_socket();
    if (sock == -1) {
        free(f->filepath);
        return NULL;
    }

    int err = connect_to_server(f->port, server_ip, sock);
    if (err != 0) {
        close(sock);
        free(f->filepath);
        return NULL;
    }

    //send file to server
    upload_file(f->filepath, sock);

    close(sock);
    free(f->filepath);
    return NULL;
}

// Download a file from the server
void* recv_file(void* in) {
    file* f = (file*) in;
    int sock = get_socket();
    if (sock == -1) {
        free(f->filepath);
        return NULL;
    }

    int err = connect_to_server(f->port, server_ip, sock);
    if (err != 0) {
        free(f->filepath);
        close(sock);
        return NULL;
    }

    download_file(f->filepath, sock, f->size);

    free(f->filepath);
    close(sock);
    return NULL;
}

// Listen message from server
void* network_listener(void* in) {
    int sock = *(int*)in;
    char buffer[MAX_RESULT_SIZE];
    while(true) {
        memset(buffer, 0, MAX_RESULT_SIZE);

        ssize_t valread = recv(sock, buffer, MAX_RESULT_SIZE, 0);
        if (valread < 0) {
            fprintf(outputError, "Error while reading\n");
            continue;
        } else if (valread == 0) {
            fprintf(output, "Connection closed\n");
            exit(EXIT_SUCCESS);
        }

        if(strncmp(buffer, STR_PUT_PORT, STR_PUT_PORT_LENGTH) == 0) {
            //Should generate ['put port', ' $port']
            char* token = strtok(buffer, ":");
            if (token == NULL || strlen(token) != STR_PUT_PORT_LENGTH || strcmp(token, STR_PUT_PORT) != 0) {
                fprintf(outputError, "Invalid put answer from server (command)\n");
                return NULL;
            }

            token = strtok(NULL, ":");
            if (token == NULL || strtok(NULL, ":") != NULL) {
                fprintf(outputError, "Invalid put answer from server (port)\n");
                return NULL;
            }

            int put_port;
            int err = sscanf(token + 1, "%d", &put_port); //Token+1 to skip the first space before port number
            if(err != 1 || put_port <= 0 || put_port > MAX_PORT_NUMBER) {
                fprintf(outputError, "Invalid port number");
                return NULL;
            } else {
                file* f = &put_file[put_index_receive];
                f->port = put_port;
                put_index_receive = (put_index_receive + 1) % MAX_PARALLEL_SIZE;
                pthread_create(&thread_id, NULL, send_file, (void*)f);
            }
        } else if (strncmp(buffer, STR_GET, STR_GET_LENGTH) == 0) {
            //Should generate ['get', 'port', '$PORT', 'size', '$SIZE']
            char* token = strtok(buffer, " :"); 
            if (token == NULL || strlen(token) != STR_GET_LENGTH || strcmp(token, STR_GET) != 0) {
                fprintf(outputError, "Invalid get answer (command)\n");
                return NULL;
            }

            token = strtok(NULL, " :");
            if(token == NULL || strlen(token) != STR_PORT_LENGTH || strcmp(token, STR_PORT) != 0) {
                fprintf(outputError, "Invalid get answer (command)\n");
                return NULL;
            }

            token = strtok(NULL, " :");
            if (token == NULL) {
                fprintf(outputError, "Invalid get answer (port)\n");
                return NULL;
            }

            int port;
            int err = sscanf(token, "%d", &port);
            if(err != 1 || port <= 0 || port > MAX_PORT_NUMBER) {
                fprintf(outputError, "Invalid port number\n");
                return NULL;
            }

            file* f = &get_file[get_index_receive];
            f->port = port;
            get_index_receive = (get_index_receive + 1) % MAX_PARALLEL_SIZE;
            token = strtok(NULL, " :");
            if (token == NULL || strlen(token) != STR_SIZE_LENGTH || strcmp(token, STR_SIZE) != 0) {
                fprintf(outputError, "Invalid get answer (size)\n");
                return NULL;
            }

            token = strtok(NULL, " :");
            if (token == NULL || strtok(NULL, " :") != NULL) {
                fprintf(outputError, "Invalid get answer (size)\n");
                return NULL;
            }

            int size;
            err = sscanf(token, "%d", &size);
            if (err != 1 || size <= 0) {
                fprintf(outputError, "Invalid size\n");
                return NULL;
            }

            f->size = size;
            pthread_create(&thread_id, NULL, recv_file, (void*)f);

        } else if (strlen(buffer) > 0) {
            fprintf(output, "%s\n", buffer);
        }
    }
    return NULL;
}

// Check validity of client command and parse them
int check_and_parse_client_command(char* command, char** filepath, char** size, char** filename, char** command_to_send) {
	char cmd[MAX_COMMAND_LENGTH];
	memset(cmd, 0, MAX_COMMAND_LENGTH);
	strcpy(cmd, command);

    char* cmd_name = strtok(cmd, " ");
    if (cmd_name == NULL) {
        return -1;
    }

    size_t len_cmd_name = strlen(cmd_name);
    for (size_t i = 0; i < nb_params; i++){
        if(strlen(correct_commands[i]) == len_cmd_name && strcmp(cmd_name, correct_commands[i]) == 0) {
            size_t nbParam = 0;
            while (strtok(NULL, " ") != NULL) {
                nbParam++;
            }
            if (nbParam == command_params[i]) {
                *command_to_send = command;
                return 0;
            } else {
                return -1;
            }
        }
    }

    //Special case for put since we need the arguments
    if(strlen("put") == len_cmd_name && strcmp(cmd_name, "put") == 0) {
        char* fp = strtok(NULL, " ");
        if (fp == NULL) {
            //Missing filepath
            return -1;
        }
        char* sz = strtok(NULL, " ");
        if(sz == NULL) {
            //Missing size
            return -1;
        }
        if(strtok(NULL, " ") != NULL) {
            //Too many arguments
            return -1;
        }
        *filepath = malloc(strlen(fp) + 1);
        strcpy(*filepath, fp);
        *size = malloc(strlen(sz) + 1);
        strcpy(*size, sz);
        (*filename = strrchr(*filepath, '/')) ? ++(*filename) : (*filename = *filepath);
        *command_to_send = malloc(strlen("put") + 1 + strlen(*filename) + 1 + strlen(*size) + 1);
        sprintf(*command_to_send, "put %s %s", *filename, *size);
        return 1;
    }

    //Special case for get since we need the arguments
    if(strlen("get") == len_cmd_name && strcmp(cmd_name, "get") == 0) {
        char* fn = strtok(NULL, " ");
        if (fn == NULL) {
            //Missing filename
            return -1;
        }
        if(strtok(NULL, " ") != NULL) {
            //Too many arguments
            return -1;
        }
        *command_to_send = command;
        *filename = malloc(strlen(fn) + 1);
        strcpy(*filename, fn);
        return 2;
    }
    
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "Invalid number of arguments: given %d, expected: 2 or 4\n", argc-1);
        exit(EXIT_FAILURE);
    }

    // Bind stderr, stdin and stdout to the given input/output files
    if (argc == 5) {
        input = fopen(argv[3], "r");
        if (input == NULL) {
            perror("Failed to open input file");
            exit(EXIT_FAILURE);
        }
        output = fopen(argv[4], "w");
        if (output == NULL) {
            perror("Faile to open output file");
            exit(EXIT_FAILURE);
        }
        outputError = output;
    } else {
        input = stdin;
        output = stdout;
        outputError = stderr;
    }

    strncpy(server_ip, argv[1], MAX_IP_LENGTH);
    const char* server_port = argv[2];

    int port = 0;
    int err;
    err = sscanf(server_port, "%d", &port);
    if (err != 1 || port <= 0 || port > MAX_PORT_NUMBER) {
        fprintf(outputError, "Invalid port number\n");
        exit(EXIT_FAILURE);
    }
    
    int sock = get_socket();
    if (sock == -1) {
        exit(EXIT_FAILURE);
    }

    err = connect_to_server(port, server_ip, sock);
    if (err != 0) {
        exit(EXIT_FAILURE);
    }

    char command[MAX_COMMAND_LENGTH];
    memset(command, 0, MAX_COMMAND_LENGTH);

    err = pthread_create(&thread_id, NULL, network_listener, (void*)&sock);
    if (err != 0){
        perror("Could not create thread");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    while (strlen(command) != STR_EXIT_LENGTH || strcmp(command, STR_EXIT) != 0) {
        fgets(command, MAX_COMMAND_LENGTH, input);

        //Remove \n
        strip_endline_char(command);
        
        char* filename = NULL;
        char* size = NULL;
        char* filepath = NULL;
        char* command_to_send = NULL;

        int res = check_and_parse_client_command(command, &filepath, &size, &filename, &command_to_send);
        if (res == 0) { //Normal command
            ssize_t valsend = send(sock, command_to_send, strlen(command_to_send)+1, 0);
            if (valsend <= 0) {
                fprintf(outputError, "Error while sending\n");
                break;
            }
        } else if (res == 1) { //Put command
            int s;
            int err = sscanf(size, "%d", &s);
            if(err != 1 || s <= 0){
                fprintf(outputError, "Invalid size");
                continue;
            }
            memset(&put_file[put_index_send], 0, sizeof(file));
            put_file[put_index_send].filepath = filepath;
            put_file[put_index_send].size = s;
            put_index_send = (put_index_send + 1) % MAX_PARALLEL_SIZE;
            free(size);

            ssize_t valsend = send(sock, command_to_send, strlen(command_to_send)+1, 0);

            free(command_to_send);
            if (valsend <= 0) {
                fprintf(outputError, "Error while sending\n");
            }
        } else if (res == 2) { //Get command
            memset(&get_file[get_index_send], 0, sizeof(file));
            get_file[get_index_send].filepath = filename;
            get_index_send = (get_index_send + 1) % MAX_PARALLEL_SIZE;

            ssize_t valsend = send(sock, command_to_send, strlen(command_to_send)+1, 0);
            if (valsend <= 0) {
                fprintf(outputError, "Error while sending\n");
                free(filename);
            }
        } else { //Bad command
            fprintf(outputError, "Bad command\n");
        }
    }

    close(sock);
    return 0;
}
