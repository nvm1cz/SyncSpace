# SyncSpace connection protocol

This first version tests TCP connectivity. Group, storage, and authentication
commands are not connected yet.

- Transport: IPv4 TCP, default port 9090.
- The server immediately sends `OK CONNECTED SyncSpace\n`.
- Each request is one text line ending in LF (`\n`); CRLF is also accepted.
- Each request receives exactly one response line ending in LF.
- Maximum request: 1023 bytes including the line ending (1022 payload bytes
  with LF). Embedded NUL bytes are invalid.
- TCP can split or combine messages; receivers collect bytes until LF.

| Request | Response | Connection |
| --- | --- | --- |
| `PING\n` | `OK PONG\n` | Stays open |
| `hello\n` | `OK ECHO hello\n` | Stays open |
| `QUIT\n` | `OK BYE\n` | Server closes it |

All lines other than PING and QUIT are echoed, including unknown commands.
Commands are case sensitive. Echo is temporary functionality for checking the
connection, not execution of group or file commands.

Malformed, oversized, or incomplete lines cause the server to attempt
`ERR Invalid or oversized line\n` and close that client connection. Clean EOF
between requests simply closes the connection. Other clients remain connected.

`send_all()` returns 0 on success and -1 on error. `recv_line()` returns the
line's byte count including LF, 0 on clean EOF, or -1 on error. `recv_exact()`
returns 1 on success, 0 on EOF before any data, or -1 on error or partial EOF.
On errors, these helpers set errno. The executables ignore SIGPIPE so a peer
disconnecting during a send cannot terminate the process.
