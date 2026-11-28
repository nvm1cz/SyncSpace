#include "commands.h"
#include "net.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int connect_server(const char *host, const char *port)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, port, &hints, &addresses);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *p = addresses; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    if (sock < 0) perror("connect");
    freeaddrinfo(addresses);
    return sock;
}

static int receive_reply(int sock, char *line)
{
    int length = recv_line(sock, line, MAX_LINE);
    if (length <= 0) return -1;
    fputs(line, stdout);
    line[length - 1] = '\0';
    return 0;
}

static int receive_list(int sock, const char *header)
{
    const char *const lists[][2] = {
        {"OK|GROUPS|", "GROUP|"},
        {"OK|REQUESTS|", "REQUEST|"},
        {"OK|MEMBERS|", "MEMBER|"},
        {"OK|FILES|", "ITEM|"},
    };

    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
        size_t prefix_length = strlen(lists[i][0]);
        if (strncmp(header, lists[i][0], prefix_length) != 0) continue;

        unsigned long long count;
        if (parse_decimal(header + prefix_length, &count) < 0 || count > INT_MAX) return -1;

        char line[MAX_LINE];
        for (unsigned long long row = 0; row < count; row++) {
            if (receive_reply(sock, line) < 0 ||
                strncmp(line, lists[i][1], strlen(lists[i][1])) != 0) return -1;
        }
        return receive_reply(sock, line) == 0 && strcmp(line, "END") == 0 ? 0 : -1;
    }
    return 0;
}

static int put_file(int sock, int gid, const char *local, const char *remote)
{
    int fd = open(local, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror(local);
        return 0;
    }

    struct stat info;
    if (fstat(fd, &info) < 0) {
        perror(local);
        close(fd);
        return 0;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0) {
        fprintf(stderr, "%s must be a regular file.\n", local);
        close(fd);
        return 0;
    }

    unsigned long long size = (unsigned long long)info.st_size;
    char line[MAX_LINE];
    int length = snprintf(line, sizeof(line), "UPLOAD|%d|%s|%llu\n", gid, remote, size);
    int result = -1;
    if (send_all(sock, line, (size_t)length) == 0 && receive_reply(sock, line) == 0) {
        if (strcmp(line, "READY") == 0) {
            unsigned char buffer[SS_TRANSFER_CHUNK];
            while (size > 0) {
                size_t chunk = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
                ssize_t read_size = read(fd, buffer, chunk);
                if (read_size < 0 && errno == EINTR) continue;
                if (read_size <= 0 || send_all(sock, buffer, (size_t)read_size) < 0) {
                    if (read_size < 0) perror(local);
                    if (read_size == 0) fprintf(stderr, "%s ended during upload.\n", local);
                    close(fd);
                    return -1;
                }
                size -= (unsigned long long)read_size;
            }
            if (receive_reply(sock, line) == 0 &&
                (strcmp(line, "OK|UPLOAD") == 0 || strncmp(line, "ERR|", 4) == 0)) {
                result = 0;
            }
        } else if (strncmp(line, "ERR|", 4) == 0) {
            result = 0;
        }
    }
    close(fd);
    return result;
}

/* Drain the advertised bytes even after a local write failure. */
static int receive_file(int sock, FILE *file, const char *local, unsigned long long size)
{
    unsigned char buffer[SS_TRANSFER_CHUNK];
    int write_failed = 0;

    while (size > 0) {
        size_t chunk = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
        if (recv_exact(sock, buffer, chunk) != 1) return -1;
        if (!write_failed && fwrite(buffer, 1, chunk, file) != chunk) {
            perror(local);
            write_failed = 1;
        }
        size -= chunk;
    }
    return write_failed;
}

static int get_file(int sock, int gid, const char *remote, const char *local)
{
    FILE *file = fopen(local, "wbx");
    if (file == NULL) {
        perror(local);
        return 0;
    }

    char line[MAX_LINE];
    int length = snprintf(line, sizeof(line), "DOWNLOAD|%d|%s\n", gid, remote);
    int result = -1;
    int keep = 0;
    if (send_all(sock, line, (size_t)length) == 0 && receive_reply(sock, line) == 0) {
        unsigned long long size;
        if (strncmp(line, "FILE|", 5) == 0 && parse_decimal(line + 5, &size) == 0) {
            int received = receive_file(sock, file, local, size);
            keep = received == 0;
            result = received < 0 ? -1 : 0;
        } else if (strncmp(line, "ERR|", 4) == 0) {
            result = 0;
        }
    }
    if (fclose(file) != 0) {
        perror(local);
        keep = 0;
    }
    if (!keep && unlink(local) != 0) perror(local);
    return result;
}

static int transfer_command(int sock, char *line)
{
    char *rest;
    char *command = strtok_r(line, " \t\n", &rest);
    char *gid_text = strtok_r(NULL, " \t\n", &rest);
    char *first = strtok_r(NULL, " \t\n", &rest);
    char *second = strtok_r(NULL, " \t\n", &rest);
    int upload = strcmp(command, "put") == 0;
    unsigned long long gid;

    if (gid_text == NULL || first == NULL || second == NULL ||
        strtok_r(NULL, " \t\n", &rest) != NULL || parse_decimal(gid_text, &gid) < 0 ||
        gid == 0 || gid > INT_MAX) {
        fputs("Usage: put <gid> <local_path> <remote_name> or get <gid> <remote_name> <local_path>\n",
              stderr);
        return 0;
    }

    const char *remote = upload ? second : first;
    size_t length = strspn(remote, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
    if (length == 0 || length > 64 || remote[length] != '\0' ||
        strcmp(remote, ".") == 0 || strcmp(remote, "..") == 0) {
        fputs("Invalid remote file name.\n", stderr);
        return 0;
    }
    if (upload) return put_file(sock, (int)gid, first, remote);
    return get_file(sock, (int)gid, remote, second);
}

/* Read a bounded command and consume the whole input line if it is rejected. */
static int read_command(char *line)
{
    int c;
    int length = 0;
    int invalid = 0;
    while ((c = getchar()) != EOF && c != '\n') {
        if (c == '\0' || length == MAX_LINE - 2) {
            invalid = 1;
        } else {
            line[length++] = (char)c;
        }
    }
    if (invalid) return -1;
    if (c == EOF && length == 0) return 0;
    line[length++] = '\n';
    line[length] = '\0';
    return length;
}

int main(int argc, char **argv)
{
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [host] [port]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *host = argc >= 2 ? argv[1] : "127.0.0.1";
    const char *port = argc == 3 ? argv[2] : "9090";
    char *end;
    errno = 0;
    long port_number = strtol(port, &end, 10);
    if (errno != 0 || end == port || *end != '\0' || port_number < 1 || port_number > 65535) {
        fprintf(stderr, "Port must be between 1 and 65535.\n");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    int sock = connect_server(host, port);
    if (sock < 0) return EXIT_FAILURE;

    char response[MAX_LINE];
    if (recv_line(sock, response, sizeof(response)) <= 0 ||
        strcmp(response, SS_GREETING) != 0) {
        fputs("Expected a SyncSpace Week 2 greeting.\n", stderr);
        close(sock);
        return EXIT_FAILURE;
    }
    fputs(response, stdout);

    char line[MAX_LINE];
    int status = EXIT_SUCCESS;
    int interactive = isatty(STDIN_FILENO);
    if (interactive) puts("Use protocol commands, put, get, or QUIT.");

    for (;;) {
        if (interactive) {
            fputs("syncspace> ", stdout);
            fflush(stdout);
        }
        int length = read_command(line);
        if (ferror(stdin)) {
            perror("stdin");
            status = EXIT_FAILURE;
            break;
        }
        if (length == 0) break;
        if (length < 0) {
            fprintf(stderr, "Invalid input (maximum %d characters, no NUL bytes).\n", MAX_LINE - 2);
            continue;
        }

        int result;
        if (strncmp(line, "put", 3) == 0 && strchr(" \t\n", line[3]) != NULL) {
            result = transfer_command(sock, line);
        } else if (strncmp(line, "get", 3) == 0 && strchr(" \t\n", line[3]) != NULL) {
            result = transfer_command(sock, line);
        } else if (strncmp(line, "UPLOAD|", 7) == 0 || strncmp(line, "DOWNLOAD|", 9) == 0) {
            fputs("Use put or get for file transfers.\n", stderr);
            continue;
        } else {
            result = send_all(sock, line, (size_t)length);
            if (result == 0) result = receive_reply(sock, response);
            if (result == 0 && strcmp(response, "OK|BYE") == 0) break;
            if (result == 0) result = receive_list(sock, response);
        }
        if (result < 0) {
            fputs("Connection closed or invalid server response.\n", stderr);
            status = EXIT_FAILURE;
            break;
        }
    }

    close(sock);
    return status;
}
