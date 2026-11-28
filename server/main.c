#include "../common/api.h"
#include "../common/commands.h"
#include "commands_groups.h"
#include "commands_storage.h"
#include "persistence.h"
#include "../common/net.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum {
    PING, QUIT, REGISTER, LOGIN, LOGOUT, CREATE_GROUP, LIST_GROUPS,
    REQUEST_JOIN, LIST_REQUESTS, APPROVE_JOIN, LIST_MEMBERS,
    MKDIR, LIST_FILES, UPLOAD, DOWNLOAD
} Command;

typedef struct {
    const char *name;
    Command command;
    int fields;
    int needs_login;
} CommandSpec;

static const CommandSpec commands[] = {
    {"PING", PING, 1, 0},
    {"QUIT", QUIT, 1, 0},
    {"REGISTER", REGISTER, 3, 0},
    {"LOGIN", LOGIN, 3, 0},
    {"LOGOUT", LOGOUT, 1, 1},
    {"CREATE_GROUP", CREATE_GROUP, 2, 1},
    {"LIST_GROUPS", LIST_GROUPS, 1, 1},
    {"REQUEST_JOIN", REQUEST_JOIN, 2, 1},
    {"LIST_REQUESTS", LIST_REQUESTS, 2, 1},
    {"APPROVE_JOIN", APPROVE_JOIN, 3, 1},
    {"LIST_MEMBERS", LIST_MEMBERS, 2, 1},
    {"MKDIR", MKDIR, 3, 1},
    {"LIST_FILES", LIST_FILES, 3, 1},
    {"UPLOAD", UPLOAD, 4, 1},
    {"DOWNLOAD", DOWNLOAD, 3, 1},
};

static int reply(int sock, const char *line)
{
    return send_all(sock, line, strlen(line));
}

static int split_fields(char *line, char **fields, int capacity)
{
    int count = 1;
    fields[0] = line;

    for (char *p = line; *p; p++) {
        if ((unsigned char)*p < ' ' || (unsigned char)*p > '~') return -1;
        if (*p == '|') {
            if (p == fields[count - 1] || count == capacity) return -1;
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return *fields[count - 1] == '\0' ? -1 : count;
}

static int parse_id(const char *text)
{
    unsigned long long id;
    if (parse_decimal(text, &id) < 0 || id == 0 || id > INT_MAX) return -1;
    return (int)id;
}

static int authenticate(int sock, int *uid, Command command, char **fields)
{
    if (command == LOGIN && *uid > 0) {
        return reply(sock, "ERR|ALREADY_AUTHENTICATED\n");
    }

    int result = command == REGISTER ? auth_register(fields[1], fields[2])
                                     : auth_login(fields[1], fields[2]);
    if (result < 0) {
        if (command == LOGIN) return reply(sock, "ERR|INVALID_CREDENTIALS\n");
        if (result == SS_ERR_EXISTS) return reply(sock, "ERR|EXISTS\n");
        if (result == SS_ERR_FULL) return reply(sock, "ERR|FULL\n");
        if (result == SS_ERR_IO) return reply(sock, "ERR|IO\n");
        return reply(sock, SS_BAD_REQUEST);
    }

    if (command == LOGIN) *uid = result;
    char response[64];
    snprintf(response, sizeof(response), "OK|%s|%d\n", fields[0], result);
    return reply(sock, response);
}

/* Return 0 to continue, or a nonzero value to close the connection. */
static int dispatch(int sock, int *uid, char *line)
{
    char *fields[4];
    int count = split_fields(line, fields, 4);
    if (count < 0) return reply(sock, SS_BAD_REQUEST);

    const CommandSpec *spec = NULL;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(fields[0], commands[i].name) == 0) {
            spec = &commands[i];
            break;
        }
    }
    if (spec == NULL) return reply(sock, "ERR|UNKNOWN_COMMAND\n");
    if (count != spec->fields) return reply(sock, SS_BAD_REQUEST);
    if (spec->needs_login && *uid < 0) return reply(sock, SS_AUTH_REQUIRED);

    switch (spec->command) {
        case PING: return reply(sock, "OK|PONG\n");
        case QUIT:
            reply(sock, "OK|BYE\n");
            return 1;
        case REGISTER:
        case LOGIN:
            return authenticate(sock, uid, spec->command, fields);
        case LOGOUT:
            *uid = -1;
            return reply(sock, "OK|LOGOUT\n");
        case CREATE_GROUP: return cmd_create_group(sock, *uid, fields[1]);
        case LIST_GROUPS: return cmd_list_groups(sock, *uid);
        default: break;
    }

    int gid = parse_id(fields[1]);
    if (gid < 0) return reply(sock, SS_BAD_REQUEST);
    switch (spec->command) {
        case REQUEST_JOIN: return cmd_request_join(sock, *uid, gid);
        case LIST_REQUESTS: return cmd_list_requests(sock, *uid, gid);
        case APPROVE_JOIN: {
            int target = parse_id(fields[2]);
            if (target < 0) return reply(sock, SS_BAD_REQUEST);
            return cmd_approve_join(sock, *uid, gid, target);
        }
        case LIST_MEMBERS: return cmd_list_members(sock, *uid, gid);
        case MKDIR: return cmd_mkdir(sock, *uid, gid, fields[2]);
        case LIST_FILES: return cmd_list_files(sock, *uid, gid, fields[2]);
        case UPLOAD: {
            unsigned long long size;
            if (parse_decimal(fields[3], &size) < 0) return reply(sock, SS_BAD_REQUEST);
            return cmd_upload(sock, *uid, gid, fields[2], size);
        }
        case DOWNLOAD: return cmd_download(sock, *uid, gid, fields[2]);
        default: return -1;
    }
}

static void *handle_client(void *arg)
{
    int sock = *(int *)arg;
    free(arg);

    if (reply(sock, SS_GREETING) == 0) {
        char line[MAX_LINE];
        int uid = -1;
        for (;;) {
            int n = recv_line(sock, line, sizeof(line));
            if (n <= 0) {
                if (n < 0) reply(sock, SS_BAD_REQUEST);
                break;
            }

            line[n - 1] = '\0';
            if (dispatch(sock, &uid, line) != 0) break;
        }
    }

    close(sock);
    return NULL;
}

int main(int argc, char **argv)
{
    long port = SYNCSPACE_PORT;
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [port] [bind_ipv4]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 2) {
        char *end;
        errno = 0;
        port = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' || port < 1 || port > 65535) {
            fprintf(stderr, "Port must be between 1 and 65535.\n");
            return EXIT_FAILURE;
        }
    }

    if (persistence_init() != 0) return EXIT_FAILURE;

    /* A disconnected peer must not terminate the server on macOS or Linux. */
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    const char *bind_ip = argc == 3 ? argv[2] : "127.0.0.1";
    if (inet_pton(AF_INET, bind_ip, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 bind address: %s\n", bind_ip);
        return EXIT_FAILURE;
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 16) < 0) {
        perror("server setup");
        close(listener);
        return EXIT_FAILURE;
    }

    pthread_attr_t attr;
    int err = pthread_attr_init(&attr);
    if (err != 0) {
        fprintf(stderr, "pthread_attr_init: %s\n", strerror(err));
        close(listener);
        return EXIT_FAILURE;
    }
    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err != 0) {
        fprintf(stderr, "pthread_attr_setdetachstate: %s\n", strerror(err));
        pthread_attr_destroy(&attr);
        close(listener);
        return EXIT_FAILURE;
    }

    printf("SyncSpace server listening on %s:%ld\n", bind_ip, port);
    fflush(stdout);

    for (;;) {
        int sock = accept(listener, NULL, NULL);
        if (sock < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        int *client_sock = malloc(sizeof(*client_sock));
        if (client_sock == NULL) {
            close(sock);
            continue;
        }
        *client_sock = sock;
        pthread_t thread;
        err = pthread_create(&thread, &attr, handle_client, client_sock);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            free(client_sock);
            close(sock);
        }
    }

    pthread_attr_destroy(&attr);
    close(listener);
    return EXIT_FAILURE;
}
