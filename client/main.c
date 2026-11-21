#include "net.h"

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

    char response[MAX_LINE + 16];
    if (recv_line(sock, response, sizeof(response)) <= 0) {
        fprintf(stderr, "Server disconnected before its greeting.\n");
        close(sock);
        return EXIT_FAILURE;
    }
    fputs(response, stdout);
    puts("Type PING, any message, or QUIT to disconnect.");

    char line[MAX_LINE];
    int status = EXIT_SUCCESS;
    int interactive = isatty(STDIN_FILENO);
    for (;;) {
        if (interactive) {
            fputs("syncspace> ", stdout);
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (ferror(stdin)) {
                perror("stdin");
                status = EXIT_FAILURE;
            }
            break;
        }
        size_t length = strlen(line);
        if (length == 0 || line[length - 1] != '\n') {
            if (length >= sizeof(line) - 1) {
                int c;
                while ((c = getchar()) != '\n' && c != EOF) {}
                fprintf(stderr, "Message too long (maximum %d characters).\n", MAX_LINE - 2);
                continue;
            }
            line[length++] = '\n';
            line[length] = '\0';
        }
        if (send_all(sock, line, length) < 0) {
            perror("send");
            status = EXIT_FAILURE;
            break;
        }
        int n = recv_line(sock, response, sizeof(response));
        if (n <= 0) {
            if (n < 0) perror("receive");
            else fprintf(stderr, "Server disconnected.\n");
            status = EXIT_FAILURE;
            break;
        }
        fputs(response, stdout);
        if (strcmp(response, "OK BYE\n") == 0) break;
    }

    close(sock);
    return status;
}
