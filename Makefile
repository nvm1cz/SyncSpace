CC = cc
CPPFLAGS += -Icommon -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g

.PHONY: all server client test-connection clean
all: server client

server: build/server
client: build/client

build:
	mkdir -p build

build/server: server/main.c common/net.c common/net.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) server/main.c common/net.c -pthread -o $@

build/client: client/main.c common/net.c common/net.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) client/main.c common/net.c -o $@

test-connection: all
	python3 tests/test_connection.py

clean:
	rm -rf build
