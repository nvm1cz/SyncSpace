#ifndef SYNCSPACE_COMMANDS_H
#define SYNCSPACE_COMMANDS_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SS_GREETING "OK|CONNECTED|SyncSpace\n"
#define SS_BAD_REQUEST "ERR|BAD_REQUEST\n"
#define SS_AUTH_REQUIRED "ERR|AUTH_REQUIRED\n"
#define SS_TRANSFER_CHUNK 65536

static inline int parse_decimal(const char *text, unsigned long long *value)
{
    if (*text == '\0' || text[strspn(text, "0123456789")] != '\0') return -1;

    errno = 0;
    char *end;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || number > UINT64_MAX) return -1;

    *value = number;
    return 0;
}

#endif
