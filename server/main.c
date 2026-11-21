#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *handle_client(void *arg)
{
    int sock = *(int *)arg;
    free(arg);

    const char *greeting = "OK CONNECTED SyncSpace\n";
    if (send_all(sock, greeting, strlen(greeting)) < 0) {
        close(sock);
        return NULL;
    }

    char line[MAX_LINE];
    for (;;) {
        int n = recv_line(sock, line, sizeof(line));
        if (n == 0) break;
        if (n < 0) {
            const char *error = "ERR Invalid or oversized line\n";
            send_all(sock, error, strlen(error));
            break;
        }

        line[n - 1] = '\0';
        if (n > 1 && line[n - 2] == '\r') line[n - 2] = '\0';

        if (strcmp(line, "QUIT") == 0) {
            const char *bye = "OK BYE\n";
            send_all(sock, bye, strlen(bye));
            break;
        }

        char response[MAX_LINE + 16];
        if (strcmp(line, "PING") == 0) {
            snprintf(response, sizeof(response), "OK PONG\n");
        } else {
            snprintf(response, sizeof(response), "OK ECHO %s\n", line);
        }
        if (send_all(sock, response, strlen(response)) < 0) break;
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
