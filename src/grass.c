#include <grass.h>
#include <stdio.h>

void hijack_flow(){
	printf("Method hijack: Accepted\n");
}

// User comparison function
int struct_user_cmp_by_name(const void* a, const void* b) {
    struct User* ia = *(struct User**)a;
    struct User* ib = *(struct User**)b;
    return strcmp(ia->uname, ib->uname);
}

// Helper function to remove \n from string
void strip_endline_char(char* str) {
    int len = strlen(str) - 1;
    if ((len >= 0) && ((str)[len] == '\n'))
        str[len] = '\0';
}

// Helper function to get a new socket
int get_socket() {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("Error while getting a socket.");
	}
	return sock;
}

// Helper function to get the port used by the server
int get_port(int sockfd, struct sockaddr_in* address, int* addrlen) {
	int err = getsockname(sockfd, (struct sockaddr*) address, (socklen_t*) addrlen);
    if (err == -1) {
        perror("Failed call to getsockname");
        return -1;
    }
    return ntohs(address->sin_port);
}

// Helper function to bind and listen
int bind_and_listen(int port, int sock, struct sockaddr_in* address, int* addrlen) {
	address->sin_family = AF_INET;
	address->sin_addr.s_addr = INADDR_ANY;
	address->sin_port = htons(port);

    *addrlen = sizeof(*address);
    
    int bind_value = bind(sock, (struct sockaddr*) address, *addrlen);
    if(bind_value < 0){
        perror("Unable to bind socket");
		return -1;
    }

    int listen_value = listen(sock, 128);
    if(listen_value < 0){
        perror("Unable to listen on socket");
		return -1;
    }

	return 0;
}

// Helper function to connect to the server
int connect_to_server(int port, const char* server_ip, int sock) {
	struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };

    int err = inet_pton(AF_INET, server_ip, &address.sin_addr);
    if (err != 1) {
        perror("Invalid IP address");
		close(sock);
		return -1;
    }

    err = connect(sock, (struct sockaddr*) &address, sizeof(address));
    if (err != 0) {
        perror("Failed to create connection");
        close(sock);
		return -1;
    }
	return 0;
}

// Helper function to upload a file
int upload_file(char* filepath, int sock) {
    FILE* fd = fopen(filepath, "rb");
    if (fd == NULL) {
        perror("Failed to open file");
        return -1;
    }

    size_t size_read;
    char buffer[MAX_BUFF_SIZE];
    while (!feof(fd)) {
        size_read = fread(buffer, sizeof(char), MAX_BUFF_SIZE, fd);
        send(sock, buffer, size_read, 0);
        memset(buffer, 0, MAX_BUFF_SIZE);
    }

    fclose(fd);
    return 0;
}

// Helper function to download a file
int download_file(char* filepath, int sockfd, int expected_size) {
	FILE* new_file = fopen(filepath, "wb");
    if (new_file == NULL) {
        perror("Failed to open file");
		return -1;
    }

    int curr_size = 0;
    while(true) {
        char buffer[MAX_BUFF_SIZE];
        memset(buffer, 0, MAX_BUFF_SIZE);
        ssize_t valread = recv(sockfd, buffer, MAX_BUFF_SIZE, 0);
        if (valread <= 0) {
            break;
        }
        curr_size += valread;
        fwrite(buffer, sizeof(char), valread, new_file);
    }
    fclose(new_file);

    if(curr_size != expected_size) {
        remove(filepath);
        return -2;
    }
    
    return 0;
}

// Helper function to get the command type
cmd_type get_cmd_type(const char* cmd, const size_t len) {
    if (len == STR_LOGIN_LENGTH && strcmp(cmd, STR_LOGIN) == 0) {
        return LOGIN;
    } else if (len == STR_PASS_LENGTH && strcmp(cmd, STR_PASS) == 0) {
        return PASS;
    } else if (len == STR_PING_LENGTH && strcmp(cmd, STR_PING) == 0) {
        return PING;
    } else if (len == STR_LS_LENGTH && strcmp(cmd, STR_LS) == 0) {
        return LS;
    } else if (len == STR_CD_LENGTH && strcmp(cmd, STR_CD) == 0) {
        return CD;
    } else if (len == STR_MKDIR_LENGTH && strcmp(cmd, STR_MKDIR) == 0) {
        return MKDIR;
    } else if (len == STR_RM_LENGTH && strcmp(cmd, STR_RM) == 0) {
        return RM;
    } else if (len == STR_GET_LENGTH && strcmp(cmd, STR_GET) == 0) {
        return GET;
    } else if (len == STR_PUT_LENGTH && strcmp(cmd, STR_PUT) == 0) {
        return PUT;
    } else if (len == STR_GREP_LENGTH && strcmp(cmd, STR_GREP) == 0) {
        return GREP;
    } else if (len == STR_DATE_LENGTH && strcmp(cmd, STR_DATE) == 0) {
        return DATE;
    } else if (len == STR_WHOAMI_LENGTH && strcmp(cmd, STR_WHOAMI) == 0) {
        return WHOAMI;
    } else if (len == STR_W_LENGTH && strcmp(cmd, STR_W) == 0) {
        return W;
    } else if (len == STR_LOGOUT_LENGTH && strcmp(cmd, STR_LOGOUT) == 0) {
        return LOGOUT;
    } else if (len == STR_EXIT_LENGTH && strcmp(cmd, STR_EXIT) == 0) {
        return EXIT;
    } else {
        return INVALID;
    }
}

// Helper function to check if the number of parameters is correct for the given command
bool check_nb_param(const size_t nbParam, cmd_type ct) {
    switch(ct) {
        case LOGIN:
            return nbParam == LOGIN_NB_PARAM;
        break;
        case PASS:
            return nbParam == PASS_NB_PARAM;
        break;
        case PING:
            return nbParam == PING_NB_PARAM || nbParam == PING_NB_PARAM+1;
        break;
        case LS:
            return nbParam == LS_NB_PARAM;
        break;
        case CD:
            return nbParam == CD_NB_PARAM;
        break;
        case MKDIR:
            return nbParam == MKDIR_NB_PARAM;
        break;
        case RM:
            return nbParam == RM_NB_PARAM;
        break;
        case GET:
            return nbParam == GET_NB_PARAM;
        break;
        case PUT:
            return nbParam == PUT_NB_PARAM;
        break;
        case GREP:
            return nbParam == GREP_NB_PARAM;
        break;
        case DATE:
            return nbParam == DATE_NB_PARAM;
        break;
        case WHOAMI:
            return nbParam == WHOAMI_NB_PARAM;
        break;
        case W:
            return nbParam == W_NB_PARAM;
        break;
        case LOGOUT:
            return nbParam == LOGOUT_NB_PARAM;
        break;
        case EXIT:
            return nbParam == EXIT_NB_PARAM;
        break;
        case INVALID:
        default:
            return false;
        break;
    }
}

// Helper function to check if the command need authentication
bool need_authentication(cmd_type ct) {
    switch(ct) {
        case LOGIN:
        case PASS:
        case PING:
        case EXIT:
            return false;
            break;
        default:
            return true;
            break;
    }
}
