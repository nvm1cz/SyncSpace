# SyncSpace

SyncSpace is a terminal client and multithreaded server for registering
accounts, managing groups, and sharing files over TCP. The server defaults to
loopback on port 9090.

## Build and run

Requires a C11 compiler and Make. On macOS, the Xcode Command Line Tools supply
both.

```sh
make
./build/server
```

In another terminal, run `./build/client`. The server accepts
`[port] [bind_ipv4]` and the client accepts `[host] [port]`.

```text
OK|CONNECTED|SyncSpace
syncspace> REGISTER|alice|demo-pass
OK|REGISTER|1
syncspace> LOGIN|alice|demo-pass
OK|LOGIN|1
syncspace> CREATE_GROUP|demo
OK|GROUP|1
syncspace> LIST_GROUPS
OK|GROUPS|1
GROUP|1|demo|2
END
syncspace> put 1 ./report.pdf report.pdf
READY
OK|UPLOAD
syncspace> get 1 report.pdf ./copy.pdf
FILE|12345
syncspace> LOGOUT
OK|LOGOUT
syncspace> QUIT
OK|BYE
```

Replace `report.pdf` with a file you have. Local paths for `put` and `get`
are entered without whitespace. Downloads require a new local filename.

To admit another user, have them register, log in and send
`REQUEST_JOIN|1`. As the leader, run `LIST_REQUESTS|1`, then
`APPROVE_JOIN|1|<user_id>`. Both users can then use `LIST_MEMBERS|1` and
`LIST_FILES|1|.`. Approved members also see joined groups through `LIST_GROUPS`.

Account, group, membership and join-request records now persist in
`storage/syncspace.db` (SQLite); uploaded files remain in `storage/groups/`.
Restart the server **from the same working directory** to preserve data.
On startup, old group files with no database are refused rather than silently
reassigned. Back up and migrate those legacy files separately.

**Security limitations:** passwords currently remain plaintext in SQLite and
on the TCP connection. Use disposable test accounts and localhost/trusted LAN
only. Before exposing a port on AWS, add password hashing and transport security.
Keep the SQLite database and file directory together on durable EBS storage.

## Tests

Python 3 is required for the TCP tests.

```sh
make test
```

Individual targets are `test-auth`, `test-groups`, `test-storage`,
`test-transfer`, `test-connection`, and `test-e2e`. TCP tests launch the
real server in a temporary directory and exercise session isolation, join
approval, list framing, file permissions, and binary transfers through the CLI.

See [the protocol reference](docs/PROTOCOL.md) for the full command list.
