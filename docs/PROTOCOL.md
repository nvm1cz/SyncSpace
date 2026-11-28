# SyncSpace Week 2 TCP protocol

The server uses IPv4 TCP and defaults to port 9090. On connection it sends
`OK|CONNECTED|SyncSpace\n`. Except during a file transfer, each message is
a case-sensitive printable ASCII line terminated by LF. The maximum request is
1023 bytes including LF. Fields are separated by `|` and must be nonempty.
Carriage returns and embedded NUL bytes are invalid. The server handles TCP
fragmentation and combined requests.

| Request | Success reply | Access |
| --- | --- | --- |
| `PING` | `OK\|PONG` | Any connection |
| `REGISTER\|username\|password` | `OK\|REGISTER\|user_id` | Any connection |
| `LOGIN\|username\|password` | `OK\|LOGIN\|user_id` | Signed out |
| `LOGOUT` | `OK\|LOGOUT` | Signed in |
| `CREATE_GROUP\|name` | `OK\|GROUP\|group_id` | Signed in |
| `LIST_GROUPS` | `OK\|GROUPS\|N`, `GROUP\|id\|name\|role` rows, `END` | Signed in |
| `REQUEST_JOIN\|group_id` | `OK\|REQUESTED` | Signed in, outside the group |
| `LIST_REQUESTS\|group_id` | `OK\|REQUESTS\|N`, `REQUEST\|user_id` rows, `END` | Group leader |
| `APPROVE_JOIN\|group_id\|user_id` | `OK\|APPROVED` | Group leader |
| `LIST_MEMBERS\|group_id` | `OK\|MEMBERS\|N`, `MEMBER\|user_id\|role` rows, `END` | Group member or leader |
| `MKDIR\|group_id\|name` | `OK\|MKDIR` | Group member or leader |
| `LIST_FILES\|group_id\|.` | `OK\|FILES\|N`, `ITEM\|type\|name\|size` rows, `END` | Group member or leader |
| `UPLOAD\|group_id\|filename\|size` | `READY`, then `OK\|UPLOAD` after the bytes | Group member or leader |
| `DOWNLOAD\|group_id\|filename` | `FILE\|size`, then file bytes | Group member or leader |
| `QUIT` | `OK\|BYE`, then EOF | Any connection |

Every list has exactly `N` rows, including zero rows for an empty list.
Role `1` means member and role `2` means leader. File types are `D` for a
directory (size zero) and `F` for a file. The current Groups implementation
includes owned groups in `LIST_GROUPS`. Approved members can use the group ID
to access member and file commands. Listing joined groups is pending in Groups.

IDs are positive decimal integers within `INT_MAX`. Usernames are
1–31 characters from `[A-Za-z0-9_-]`. Passwords are 1–128 printable ASCII
characters and cannot contain a pipe. Username comparisons are case sensitive.
Registration creates an account. Login attaches the account to the current
socket until logout or disconnect. A signed-in client must log out before
logging in again.

Unknown commands return `ERR|UNKNOWN_COMMAND`. Invalid syntax returns
`ERR|BAD_REQUEST`. An invalid line containing NUL, an overlong line, or EOF
mid-line causes the server to attempt `ERR|BAD_REQUEST` and close the socket.
Complete LF-terminated commands with invalid fields leave the connection open.
Authentication failures use `ERR|INVALID_CREDENTIALS`,
`ERR|ALREADY_AUTHENTICATED`, or `ERR|AUTH_REQUIRED`. Group and storage
failures can return `ERR|FORBIDDEN`, `ERR|NOT_FOUND`, `ERR|EXISTS`,
`ERR|FULL`, or `ERR|IO`.

## Binary transfers

A size is an unsigned 64-bit decimal byte count. Names are single components
of 1–64 characters from `[A-Za-z0-9._-]`. The special names `.`, `..`,
and names ending in `.part` are reserved. Transfers address the group's
root directory and an upload never overwrites an existing file.

For an upload, wait for `READY\n` before sending exactly the declared number
of bytes. An `ERR|...\n` before `READY` means the client sends zero payload
bytes. After the server consumes the payload it replies with `OK|UPLOAD\n`
or a final error. If transfer I/O fails before the payload is consumed,
the server closes the socket and removes the temporary upload.

For a download, consume exactly the byte count after `FILE|size\n`. The
server sends the bytes immediately after the header. There is no trailing
newline or completion reply. A zero-byte transfer still uses its normal
header. Both programs stream in chunks no larger than 64 KiB.
