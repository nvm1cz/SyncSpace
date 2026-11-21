# SyncSpace

The current runnable milestone is a terminal TCP client and a server with one
detached pthread per connection. It supports PING, message echo, and QUIT.
Existing group/storage modules are not connected to these commands yet.

## Build

Requires a C compiler and Make (Xcode Command Line Tools on macOS).

```sh
make
```

## Run locally

In terminal 1:

```sh
./build/server
```

In terminal 2:

```sh
./build/client
```

Example client session:

```text
OK CONNECTED SyncSpace
Type PING, any message, or QUIT to disconnect.
syncspace> PING
OK PONG
syncspace> hello
OK ECHO hello
syncspace> QUIT
OK BYE
```

Open additional clients in other terminals to test simultaneous connections.
Use Ctrl+C in the server terminal to stop it; Ctrl+D exits the client on EOF.

Optional arguments:

```sh
./build/server 9091 127.0.0.1
./build/client localhost 9091
```

For a LAN demonstration, bind the server with `./build/server 9090 0.0.0.0`
and connect using `./build/client <server-LAN-IPv4> 9090` on the other computer.
Both computers need a reachable network path and the server firewall must allow
the port. The default bind address is loopback (127.0.0.1).

## Verify the connection

Requires Python 3:

```sh
make test-connection
```

This launches a temporary server and checks the client executable, greetings,
PING, echo, QUIT, simultaneous clients, TCP fragmentation, combined requests,
malformed messages, and disconnect handling. It stops the test server afterward.

See docs/PROTOCOL.md for message formats. This milestone does not implement
login, encryption, file transfers, or group/storage command handling.
