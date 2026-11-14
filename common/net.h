#ifndef SYNCSPACE_NET_H
#define SYNCSPACE_NET_H
 
#include <stddef.h>
#define SYNCSPACE_PORT 9090
#define MAX_LINE 1024
 
int send_all(int sock, const void *data, size_t size);
int recv_line(int sock, char *buffer, int capacity);
int recv_exact(int sock, void *buffer, size_t size);
 
#endif
