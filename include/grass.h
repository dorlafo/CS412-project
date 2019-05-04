#ifndef GRASS_H
#define GRASS_H

#define DEBUG true

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define MAX_PORT_NUMBER 65535
#define MAX_BUFF_SIZE 1024
#define MAX_COMMAND_LENGTH 1024
#define MAX_RESULT_SIZE 65536
#define MAX_CMD_LENGTH 100
#define MAX_NB_ARGS 2
#define STR_PING "ping"
#define STR_PING_LENGTH 4
#define STR_LS "ls"
#define STR_LS_LENGTH 2
#define STR_LOGIN "login"
#define STR_LOGIN_LENGTH 5
#define STR_MKDIR "mkdir"
#define STR_MKDIR_LENGTH 5
#define STR_CD "cd"
#define STR_CD_LENGTH 2
#define STR_RM "rm"
#define STR_RM_LENGTH 2
#define STR_GREP "grep"
#define STR_GREP_LENGTH 4
#define STR_DATE "date"
#define STR_DATE_LENGTH 4
#define STR_WHOAMI "whoami"
#define STR_WHOAMI_LENGTH 6
#define STR_EXIT "exit"
#define STR_EXIT_LENGTH 4
#define STR_LOGOUT "logout"
#define STR_LOGOUT_LENGTH 6
#define STR_W "w"
#define STR_W_LENGTH 1
#define STR_PUT "put"
#define STR_PUT_LENGTH 3
#define STR_GET "get"
#define STR_GET_LENGTH 3
#define STR_PORT "port"
#define STR_PORT_LENGTH 4
#define STR_PUT_PORT "put port"
#define STR_PUT_PORT_LENGTH 8
#define STR_SIZE "size"
#define STR_SIZE_LENGTH 4
#define STR_PASS "pass"
#define STR_PASS_LENGTH 4
#define LOGIN_NB_PARAM 1
#define PASS_NB_PARAM 1
#define PING_NB_PARAM 1
#define LS_NB_PARAM 0
#define CD_NB_PARAM 1
#define MKDIR_NB_PARAM 1
#define RM_NB_PARAM 1
#define GET_NB_PARAM 1
#define PUT_NB_PARAM 2
#define GREP_NB_PARAM 1
#define DATE_NB_PARAM 0
#define WHOAMI_NB_PARAM 0
#define W_NB_PARAM 0
#define LOGOUT_NB_PARAM 0
#define EXIT_NB_PARAM 0
#define ERROR_ALREADY_CONNECTED "Error: You are already connected."
#define ERROR_AUTH_FAILED "Authentication failed."
#define ERROR_ACCESS_DENIED "Error: access denied."
#define ERROR_DIR_NOT_EXISTS "Error: Directory does not exists."
#define ERROR_PATH_TOO_LONG "Error: the path is too long."
#define ERROR_FILE_TRANSFER_FAILED "Error: file transfer failed."
#define ERROR_INVALID_COMMAND "Error: invalid command"

struct User {
    const char* uname;
    const char* pass;
    bool isLoggedIn;
};

typedef enum {
    LOGIN = 0,
    PASS,
    PING,
    LS,
    CD,
    MKDIR,
    RM,
    GET,
    PUT,
    GREP,
    DATE,
    WHOAMI,
    W,
    LOGOUT,
    EXIT,
    INVALID
} cmd_type;

void hijack_flow();

int struct_user_cmp_by_name(const void*a, const void*b);

void strip_endline_char(char* str);

int get_socket();

int get_port(int sockfd, struct sockaddr_in* address, int* addrlen);

int bind_and_listen(int port, int sock, struct sockaddr_in* address, int* addrlen);

int connect_to_server(int port, const char* server_ip, int sock);

int upload_file(char* filepath, int sock);

int download_file(char* filepath, int sockfd, int expected_size);

cmd_type get_cmd_type(const char* cmd, const size_t len);

bool check_nb_param(const size_t nbParam, cmd_type ct);

bool need_authentication(cmd_type ct);

#endif
