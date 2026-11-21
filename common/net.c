#include "net.h"

#include <errno.h>
#include <sys/socket.h>

int send_all(int sock, const void *data, size_t size)
{
    const char *bytes = data;
    size_t sent = 0;
    while (sent < size) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(sock, bytes + sent, size - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(sock, bytes + sent, size - sent, 0);
#endif
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            errno = EPIPE;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int recv_line(int sock, char *buffer, int capacity)
{
    if (buffer == NULL || capacity < 2) {
        errno = EINVAL;
        return -1;
    }

    int used = 0;
    buffer[0] = '\0';
    while (used < capacity - 1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            if (used == 0) return 0;
            errno = EPROTO;
            return -1;
        }
        if (c == '\0') {
            errno = EPROTO;
            return -1;
        }
        buffer[used++] = c;
        buffer[used] = '\0';
        if (c == '\n') return used;
    }

    errno = EMSGSIZE;
    return -1;
}

int recv_exact(int sock, void *buffer, size_t size)
{
    char *bytes = buffer;
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(sock, bytes + received, size - received, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            if (received == 0) return 0;
            errno = EPROTO;
            return -1;
        }
        received += (size_t)n;
    }
    return 1;
}
