#ifndef SYNCSPACE_NET_H
#define SYNCSPACE_NET_H
 
#include <stddef.h>
#define SYNCSPACE_PORT 9090
#define MAX_LINE 1024
 
/* send_all: 0 on success, -1 on error (errno is set). */
int send_all(int sock, const void *data, size_t size);
/* recv_line: bytes including '\n', 0 on clean EOF, -1 on error.
 * Adds a NUL terminator. Rejects embedded NUL, oversized lines and partial EOF.
 * Close the connection after an error; a rejected line may be partly unread.
 */
int recv_line(int sock, char *buffer, int capacity);
/* recv_exact: 1 on success, 0 on EOF before any data, -1 on error/partial EOF. */
int recv_exact(int sock, void *buffer, size_t size);
 
#endif
