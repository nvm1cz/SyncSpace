CC = cc
CPPFLAGS += -Icommon -Iserver -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g

NET = common/net.c
GROUPS = server/groups.c
STORAGE = server/storage.c
TRANSFER = server/transfer.c server/commands_storage.c
AUTH = server/auth.c
SERVER = server/main.c server/commands_groups.c $(AUTH) $(GROUPS) $(STORAGE) $(TRANSFER) $(NET)
HEADERS = $(wildcard common/*.h server/*.h)

GROUP_TESTS = build/test_groups build/test_group_transaction build/test_group_members
TRANSFER_TESTS = build/test_transfer $(patsubst tests/test_storage2/%.c,build/%,$(wildcard tests/test_storage2/test_*.c))

.PHONY: all server client test test-auth test-groups test-storage test-transfer test-connection test-e2e clean
all: server client

server: build/server
client: build/client

build:
	mkdir -p build

build/server: $(SERVER) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -pthread -o $@

build/client: client/main.c $(NET) common/commands.h common/net.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@

build/test_auth: tests/test_auth.c $(AUTH) common/api.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -pthread -o $@

$(GROUP_TESTS): build/%: tests/%.c $(GROUPS) tests/mocks/mock_storage.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -pthread -o $@

build/test_storage: tests/test_storage.c $(STORAGE) common/api.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@

build/test_transfer: tests/test_transfer.c $(TRANSFER) $(STORAGE) $(NET) tests/mocks/mock_groups.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -pthread -o $@

build/test_c%: tests/test_storage2/test_c%.c $(TRANSFER) $(STORAGE) $(NET) tests/mocks/mock_groups.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -pthread -o $@

# Each storage test gets a fresh working directory, even under make -j.
define run_isolated
	@set -e; for test in $^; do \
		directory=$$(mktemp -d); \
		(trap 'rm -rf "$$directory"' EXIT; cd "$$directory" && "$(CURDIR)/$$test"); \
	done
endef

test: test-auth test-groups test-storage test-transfer test-e2e

test-auth: build/test_auth
	./build/test_auth

test-groups: $(GROUP_TESTS)
	@set -e; for test in $^; do "./$$test"; done

test-storage: build/test_storage
	$(run_isolated)

test-transfer: $(TRANSFER_TESTS)
	$(run_isolated)

test-connection: all
	python3 -B tests/test_connection.py

test-e2e: test-connection
	python3 -B tests/test_e2e.py

clean:
	rm -rf build
