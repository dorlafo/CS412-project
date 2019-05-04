#include "../include/grass.h"
#include <ctype.h>

#define MAX_NUM_USER 100
#define MAX_LINE_SIZE 50
#define MAX_BASE_DIR_SIZE 128
#define STR_REDIRECT "2>&1"
#define STR_PING_ADDITION "-c 1"
#define STR_LS_ADDITION "-l"
#define STR_RM_ADDITION "-r"
#define STR_GREP_ADDITION_1 "&&"
#define STR_GREP_ADDITION_2 "-lr"
#define STR_GREP_ADDITION_3 "| sort"
#define MAX_GET_ANSWER_LENGTH 40

#define MAX_PATH 128

static struct User **userlist;
static int numUsers;
int port = 0;

char baseDir[MAX_BASE_DIR_SIZE];

// Struct srv_file for put/get
typedef struct {
    char* filename;
    int sock;
    int main_sock;
    int size;
    struct sockaddr_in address;
    int addrlen;
} srv_file;

// Helper function to get name of connected users
char* get_connected_users() {
    char* res = malloc(1);
    res[0] = '\0';
    int currentLength = 0;
    for(int i = 0; i < numUsers; ++i) {
        if (userlist[i]->isLoggedIn) {
            currentLength += strlen(userlist[i]->uname) + 2;
            res = realloc(res, currentLength);
            strcat(res, userlist[i]->uname);
            strcat(res, " ");
        }
    }
    return res;
}

// Helper function to run commands in unix.
char* run_command(const char* command) {
    char* data = calloc(MAX_RESULT_SIZE, sizeof(char));

    // File where the shell output is written
    FILE* stream;
    
    char buffer[MAX_BUFF_SIZE];
    memset(buffer, 0, MAX_BUFF_SIZE);

    // Execute command and return output
    stream = popen(command, "r");
    if(!stream) {
        perror("Open stream");
        return data;
    }

    while (!feof(stream) && !ferror(stream)){
        if (fgets(buffer, MAX_BUFF_SIZE, stream) != NULL){
            strcat(data,buffer);
        } 
    }
    pclose(stream);

    return data;
}

// Helper function to send result from run_command
int send_result(char* data, int sock) {
    int datalen = strlen(data);
    if (datalen == 0) {
        return send(sock, "", 1, 0);
    } else {
        strip_endline_char(data);
        printf("%s\n", data);
        return send(sock, data, datalen+1, 0);
    }
}

// Helper function to print and send a message
void print_and_send(int sock, const char* msg) {
    printf("%s\n", msg);
    send(sock, msg, strlen(msg)+1, 0);
}

// Send a file to the client
void* send_file(void* in) {
    srv_file* f = (srv_file*) in;
    int new_socket = accept(f->sock, (struct sockaddr*) &f->address, (socklen_t*) &f->addrlen);

    upload_file(f->filename, new_socket);

    close(new_socket);
    close(f->sock);
    free(f->filename);
    free(f);
    return NULL;
}

// Send a file to the server
void* recv_file(void* in) {
    srv_file* f = (srv_file*) in;

    int new_socket = accept(f->sock, (struct sockaddr*) &f->address, (socklen_t*) &f->addrlen);
    if (download_file(f->filename, new_socket, f->size) == -2) {
        //Invalid size
        print_and_send(f->main_sock, ERROR_FILE_TRANSFER_FAILED);
    }
    close(new_socket);
    close(f->sock);
    free(f->filename);
    free(f);
    return NULL;
}

// Helper function to check if given username is in user database
struct User* check_username(const char* username) {
    size_t usernameLen = strlen(username);
    for(int i = 0; i < numUsers; ++i) {
        size_t len = strlen(userlist[i]->uname);
        if (len == usernameLen && strncmp(userlist[i]->uname, username, len) == 0)
            return userlist[i];
    }
    return NULL;
}

// Handle get request
void server_handle_get(int sock, char* filename, char* currFolder, pthread_t* thread_id) {
    srv_file* f = malloc(sizeof(srv_file));
    f->filename = malloc(strlen(currFolder) + 1 + strlen(filename) + 1);
    sprintf(f->filename, "%s/%s", currFolder, filename);

    f->sock = get_socket();
    if (f->sock == -1) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        free(f->filename);
        free(f);
        return;
    }

    int err = bind_and_listen(0, f->sock, &f->address, &f->addrlen);
    if (err == -1) {
        close(f->sock);
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        free(f->filename);
        free(f);
        return;
    }

    int new_port = get_port(f->sock, &f->address, &f->addrlen);
    if (new_port == -1) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        close(f->sock);
        free(f->filename);
        free(f);
        return;
    }
    
    char get_str[MAX_GET_ANSWER_LENGTH];
    struct stat st;
    err = stat(f->filename, &st);
    if (err != 0) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        close(f->sock);
        free(f->filename);
        free(f);
        return;
    }

    f->size = st.st_size;
    sprintf(get_str, "get port: %d size: %d", new_port, f->size);
    
    err = pthread_create(thread_id, NULL, send_file, (void*) f);
    if (err != 0){
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        perror("Could not create thread for get");
        close(f->sock);
        free(f->filename);
        free(f);
        return;
    }

    send(sock, get_str, strlen(get_str)+1, 0);
}

// Handle put request
void server_handle_put(int sock, char* filename, char* size_c, char* currFolder, pthread_t* thread_id) {
    if (strlen(currFolder) + 1 + strlen(filename) > MAX_PATH) {
        print_and_send(sock, ERROR_PATH_TOO_LONG);
        return;
    }

    srv_file* f = malloc(sizeof(srv_file));

    int size;
    int err = sscanf(size_c, "%d", &size);
    if (err != 1 || size <= 0) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        fprintf(stderr, "Invalid size");
        free(f);
        return;
    }

    f->size = size;
    printf("Size: <%d>, filename: <%s>\n", f->size, filename);
    f->sock = get_socket();
    if (f->sock == -1) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        free(f);
        return;
    }

    err = bind_and_listen(0, f->sock, &f->address, &f->addrlen);
    if (err == -1) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        close(f->sock);
        free(f);
        return;
    }

    int new_port = get_port(f->sock, &f->address, &f->addrlen);
    if (new_port == -1) {
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        close(f->sock);
        free(f);
        return;
    }

    char put_str[16];
    sprintf(put_str, "put port: %d", new_port);
    
    f->filename = malloc(strlen(currFolder) + 1 + strlen(filename) + 1);
    sprintf(f->filename, "%s/%s", currFolder, filename);
    f->main_sock = sock;

    err = pthread_create(thread_id, NULL, recv_file, (void*) f);
    if (err != 0){
        perror("Could not create thread for put");
        print_and_send(sock, ERROR_FILE_TRANSFER_FAILED);
        close(f->sock);
        free(f->filename);
        free(f);
        return;
    }
    
    send(sock, put_str, strlen(put_str) + 1, 0);
}

// Check, parse and execute command from client
int check_and_parse_command(char* command, char* currFolder, struct User** actual_user, int sock, const char* baseDir, pthread_t* thread_id) {
    char cmd[MAX_CMD_LENGTH];
	memset(cmd, 0, MAX_CMD_LENGTH);
	strcpy(cmd, command);

    char* cmd_name = strtok(cmd, " ");
    if (cmd_name == NULL) {
        print_and_send(sock, ERROR_INVALID_COMMAND);
        return 0;
    }

    size_t len_cmd_name = strlen(cmd_name);
    cmd_type ct = get_cmd_type(cmd_name, len_cmd_name);

    char* parsedArgs[] = {"", ""};
    size_t nbParam = 0;
    char* args;
    while ((args = strtok(NULL, " ")) != NULL) {
        if (nbParam < MAX_NB_ARGS)
            parsedArgs[nbParam] = args;
        nbParam++;
    }

    if (!check_nb_param(nbParam, ct)) {
        print_and_send(sock, ERROR_INVALID_COMMAND);
        return 0;
    }

    if(need_authentication(ct) && (*actual_user == NULL || (*actual_user)->isLoggedIn == false)) {
        if(*actual_user != NULL) {
            *actual_user = NULL;
        }
        print_and_send(sock, ERROR_ACCESS_DENIED);
        return 0;
    }

    char res[MAX_COMMAND_LENGTH];
    memset(res, 0, MAX_COMMAND_LENGTH);
    char* result;
    char* users;

    switch(ct) {
        case LOGIN:
            if (*actual_user != NULL && (*actual_user)->isLoggedIn == true) {
                print_and_send(sock, ERROR_ALREADY_CONNECTED);
                return 0;
            }

            *actual_user = check_username(parsedArgs[0]);
            if (*actual_user != NULL && (*actual_user)->isLoggedIn == false) {
                send(sock, "", 1, 0);
                return 0;
            }

            print_and_send(sock, ERROR_AUTH_FAILED);
            if (*actual_user != NULL) {
                (*actual_user) = NULL;
            }
            return 0;
            break;

        case PASS:
            if (*actual_user == NULL) {
                print_and_send(sock, ERROR_ACCESS_DENIED);
                return 0;
            }

            if ((*actual_user)->isLoggedIn == true) {
                print_and_send(sock, ERROR_ALREADY_CONNECTED);
                return 0;
            }

            if(strlen((*actual_user)->pass) == strlen(parsedArgs[0]) && strcmp(parsedArgs[0], (*actual_user)->pass) == 0) {
                (*actual_user)->isLoggedIn = true;
                send(sock, "", 1, 0);
                return 0;
            }

            print_and_send(sock, ERROR_AUTH_FAILED);
            actual_user = NULL;
            return 0;
            break;

        case PING:
            sprintf(res, "%s %s %s %s %s", cmd_name, parsedArgs[0], parsedArgs[1], STR_PING_ADDITION, STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case LS:
            sprintf(res, "%s %s %s %s", command, STR_LS_ADDITION, currFolder, STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case CD:
            sprintf(res, "%s %s/%s %s", cmd_name, currFolder, parsedArgs[0], STR_REDIRECT);
            char* check_dir = run_command(res);
            char* newDir = parsedArgs[0];
            printf("check_dir:\n<%s>\n", check_dir);
            if (strlen(newDir) == strlen("..") && strcmp(newDir, "..") == 0) {
                if (strlen(baseDir) != strlen(currFolder)){
                    printf("Old directory %s \n", currFolder);
                    char* last = strrchr(currFolder, '/');
                    size_t old_len = strlen(currFolder); 
                    size_t last_len = strlen(last);
                    currFolder[old_len-last_len] = '\0';
                    printf("New directory %s \n", currFolder);
                }
                send(sock, "", 1, 0);
                free(check_dir);
                return 0;
            } else if (strlen(check_dir) == 0) {
                if (newDir[0] != '/') {
                    strcat(currFolder, "/");
                }
                strcat(currFolder, newDir);
                send(sock, "", 1, 0);
                free(check_dir);
                return 0;
            }
            
            print_and_send(sock, ERROR_DIR_NOT_EXISTS);
            return 0;
            break;

        case MKDIR:
            if (strlen(currFolder) + 1 + strlen(parsedArgs[0]) > MAX_PATH) {
                print_and_send(sock, ERROR_PATH_TOO_LONG);
                return 0;
            }

            sprintf(res, "%s %s/%s %s", cmd_name, currFolder, parsedArgs[0], STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case RM:
            sprintf(res, "%s %s %s/%s %s", cmd_name, STR_RM_ADDITION, currFolder, parsedArgs[0], STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case GET:
            server_handle_get(sock, parsedArgs[0], currFolder, thread_id);
            return 0;
            break;

        case PUT:
            server_handle_put(sock, parsedArgs[0], parsedArgs[1], currFolder, thread_id);
            return 0;
            break;

        case GREP:
            //cd currentpath && grep -lr $pattern | sort $redirect
            sprintf(res, "%s %s %s %s %s %s %s %s", STR_CD, currFolder, STR_GREP_ADDITION_1, cmd_name, STR_GREP_ADDITION_2, parsedArgs[0], STR_GREP_ADDITION_3, STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case DATE:
            sprintf(res, "%s %s", cmd_name, STR_REDIRECT);
            result = run_command(res);
            send_result(result, sock);
            free(result);
            return 0;
            break;

        case WHOAMI:
            print_and_send(sock, (*actual_user)->uname);
            return 0;
            break;

        case W:
            users = get_connected_users();
            if (strlen(users) == 0) {
                send(sock, "", 1, 0);
                free(users);
                return 0;
            }

            print_and_send(sock, users);
            free(users);
            return 0;
            break;

        case LOGOUT:
            (*actual_user)->isLoggedIn = false;
            (*actual_user) = NULL;
            send(sock, "", 1, 0);
            return 0;
            break;

        case EXIT:
            if (*actual_user != NULL && (*actual_user)->isLoggedIn == true) {
                (*actual_user)->isLoggedIn = false;
                (*actual_user) = NULL;
            }
            close(sock);
            return 1;
            break;

        case INVALID:
        default:
            print_and_send(sock, ERROR_INVALID_COMMAND);
            return 0;
            break;
    }
}

// Server side REPL runnning in its own thread for each client
void* connection_handler(void* sockfd) {
    char buffer[MAX_BUFF_SIZE];

    if (sockfd == NULL){
        fprintf(stderr, "Bad socket");
        return NULL;
    } 

    int sock = *(int*) sockfd;
    struct User* actual_user = NULL;
    char actualDir[MAX_BASE_DIR_SIZE];
    memset(actualDir, 0, MAX_BASE_DIR_SIZE);
    strncpy(actualDir, baseDir, strlen(baseDir));
    pthread_t thread_id;
    int exit_value = 0;
    while(exit_value != 1) {
        memset(buffer, 0, MAX_BUFF_SIZE);
        ssize_t valread = recv(sock, buffer, MAX_BUFF_SIZE, 0);
        if (valread <= 0) {
            break;
        }
        char* cmd = buffer;
        while(strlen(cmd) != 0) {
            printf(cmd);
            printf("\n");

            exit_value = check_and_parse_command(cmd, actualDir, &actual_user, sock, baseDir, &thread_id);
            if(exit_value == 1) {
                break;
            }
            cmd = strchr(cmd, '\0');
            cmd++; //skip the \0
        }
    }

    //Disconnect user
    if(actual_user != NULL) {
        actual_user->isLoggedIn = false;
    }
    return NULL;
}

// Parse the grass.conf file and fill in the global variables
void parse_grass() {
    numUsers = 0;
    userlist = calloc(MAX_NUM_USER, sizeof(struct User*));

    memset(baseDir, 0, MAX_BASE_DIR_SIZE);

    FILE* grass = fopen("grass.conf", "r");
    if (grass == NULL) {
        perror("Failed to open grass.conf");
        exit(EXIT_FAILURE);
    } 

    char line[MAX_LINE_SIZE];

    while (fgets(line, MAX_LINE_SIZE, grass) != NULL) {
        if ((line[0] != '#') && (strlen(line) > 1)) {
            char* word = strtok(line, " ");
            if(word == NULL) {
                fprintf(stderr, "Error during parsing. Invalid file format\n");
                fclose(grass);
                exit(EXIT_FAILURE);
            }
            if (strlen(word) == strlen("base") && strncmp(word, "base", 4) == 0) {
                if (strlen(baseDir) != 0) {
                    fprintf(stderr, "Error during parsing. Base directory was already defined\n");
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }
                char* base = strtok(NULL, " "); 
                //remove \n from password:
                strip_endline_char(base);

                strncpy(baseDir, base, MAX_BASE_DIR_SIZE);
            } else if (strlen(word) == strlen("port") && strncmp(word, "port", 4) == 0) {
                if (port != 0) {
                    fprintf(stderr, "Error during parsing. Port was already defined\n");
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }
                char* port_temp = strtok(NULL, " ");
                int err = sscanf(port_temp, "%d", &port);
                if(err != 1 || port <= 0 || port > MAX_PORT_NUMBER) {
                    fprintf(stderr, "Error during parsing. Invalid port number\n");
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }
            } else if (strlen(word) == strlen("user") && strncmp(word, "user", 4) == 0) {
                char* username = strtok(NULL, " ");
                if (username == NULL) {
                    fprintf(stderr, "Error during parsing. Invalid user format\n");
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }
                if (check_username(username) != NULL) {
                    fprintf(stderr, "Error during parsing. The user '%s' was already defined\n", username);
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }

                char* password = strtok(NULL, " ");
                if (password == NULL) {
                    fprintf(stderr, "Error during parsing. Invalid user format\n");
                    fclose(grass);
                    exit(EXIT_FAILURE);
                }
                //remove \n from password:
                strip_endline_char(password);
                
                struct User* newUser = malloc(sizeof(struct User));
                int len_user = strlen(username) + 1;
                int len_password = strlen(password) + 1;
                char* temp_username = malloc(len_user);
                char* temp_password = malloc(len_password);
                
                strncpy(temp_username, username, len_user);
                strncpy(temp_password, password, len_password);

                newUser->uname = temp_username;
                newUser->pass = temp_password;
                newUser->isLoggedIn = false;
                userlist[numUsers] = newUser;
                numUsers++;
            } else {
                fprintf(stderr,"Invalid line in grass.conf");
                fclose(grass);
                exit(EXIT_FAILURE);
            }
        }
    }

    fclose(grass);

    if (strlen(baseDir) == 0 || port == 0) {
        fprintf(stderr, "Invalid grass.conf. Missing required parameter\n");
        exit(EXIT_FAILURE);
    }
    if (numUsers == 0) {
        fprintf(stdout, "[WARNING] No user in grass.conf");
    } else {
        //Sort user in alphabetical order
        qsort(userlist, numUsers, sizeof(struct User*), struct_user_cmp_by_name);
    }
}

int main() {
    // Parse the grass.conf file
    parse_grass();
    
    int sockfd = get_socket();
    if (sockfd == -1) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    int addrlen;
    int err = bind_and_listen(port, sockfd, &address, &addrlen);
    if (err == -1) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    int new_socket;
    pthread_t thread_id;

    // Listen to the port and handle each connection in a new thread
    while ((new_socket = accept(sockfd, (struct sockaddr*) &address, (socklen_t*) &addrlen))) {
        int err = pthread_create(&thread_id, NULL, connection_handler, (void*)&new_socket);
        if (err != 0){
            perror("could not create thread");
        }
    }

    close(sockfd);
    return 0;
}
