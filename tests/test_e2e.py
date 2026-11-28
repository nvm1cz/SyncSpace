"""Week 2 TCP and CLI tests against the server linked with real modules."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import socket
import subprocess
from tempfile import TemporaryDirectory

from test_connection import ROOT, launch_server, line


def request(sock, command, expected):
    sock.sendall(command.encode("ascii") + b"\n")
    actual = line(sock)
    assert actual == expected.encode("ascii") + b"\n", (command, actual)


def upload(sock, name, payload):
    request(sock, f"UPLOAD|1|{name}|{len(payload)}", "READY")
    sock.sendall(payload)
    assert line(sock) == b"OK|UPLOAD\n"


def test_sessions(connect):
    with connect() as leader, connect() as member, connect() as guest:
        request(leader, "REGISTER|leader|first pass", "OK|REGISTER|1")
        request(leader, "REGISTER|leader|again", "ERR|EXISTS")
        request(leader, "LIST_GROUPS", "ERR|AUTH_REQUIRED")
        request(member, "REGISTER|member|second", "OK|REGISTER|2")
        request(member, "LOGIN|member|wrong", "ERR|INVALID_CREDENTIALS")
        request(member, "LOGIN|member|second", "OK|LOGIN|2")
        request(leader, "LOGIN|leader|first pass", "OK|LOGIN|1")
        request(leader, "LOGIN|member|second", "ERR|ALREADY_AUTHENTICATED")
        request(guest, "LIST_MEMBERS|1", "ERR|AUTH_REQUIRED")
        request(guest, "DOWNLOAD|1|file", "ERR|AUTH_REQUIRED")

        for command in ("PING|", "PING\r", "REGISTER|x|", "CREATE_GROUP||",
                        "MKDIR|0|docs", "MKDIR|+1|docs", "MKDIR|1x|docs",
                        "MKDIR|999999999999999|docs", "APPROVE_JOIN|1|0",
                        "UPLOAD|1|file|-1", "UPLOAD|1|file|18446744073709551616"):
            request(leader, command, "ERR|BAD_REQUEST")

        request(leader, "CREATE_GROUP|team", "OK|GROUP|1")
        request(leader, "LIST_GROUPS", "OK|GROUPS|1")
        assert [line(leader), line(leader)] == [b"GROUP|1|team|2\n", b"END\n"]
        request(leader, "LOGOUT", "OK|LOGOUT")
        request(leader, "LOGOUT", "ERR|AUTH_REQUIRED")
        request(member, "LIST_GROUPS", "OK|GROUPS|0")
        assert line(member) == b"END\n"
        request(leader, "LOGIN|leader|first pass", "OK|LOGIN|1")
        request(member, "LIST_MEMBERS|1", "ERR|FORBIDDEN")
        request(member, "MKDIR|1|docs", "ERR|FORBIDDEN")
        request(member, "REQUEST_JOIN|1", "OK|REQUESTED")
        request(member, "REQUEST_JOIN|1", "ERR|EXISTS")
        request(member, "LIST_REQUESTS|1", "ERR|FORBIDDEN")
        request(member, "APPROVE_JOIN|1|2", "ERR|FORBIDDEN")
        request(leader, "LIST_REQUESTS|1", "OK|REQUESTS|1")
        assert [line(leader), line(leader)] == [b"REQUEST|2\n", b"END\n"]
        request(leader, "APPROVE_JOIN|1|2", "OK|APPROVED")
        request(leader, "APPROVE_JOIN|1|2", "ERR|NOT_FOUND")
        request(member, "LIST_MEMBERS|1", "OK|MEMBERS|2")
        assert [line(member), line(member), line(member)] == [
            b"MEMBER|1|2\n", b"MEMBER|2|1\n", b"END\n",
        ]
        request(member, "MKDIR|1|docs", "OK|MKDIR")
        request(member, "LIST_FILES|1|.", "OK|FILES|1")
        assert [line(member), line(member)] == [b"ITEM|D|docs|0\n", b"END\n"]

        with ThreadPoolExecutor(max_workers=2) as pool:
            first = pool.submit(upload, leader, "first.bin", b"A" * 65543)
            second = pool.submit(upload, member, "second.bin", b"B" * 65543)
            first.result()
            second.result()

        with connect() as dropped:
            request(dropped, "LOGIN|leader|first pass", "OK|LOGIN|1")
            request(dropped, "UPLOAD|1|incomplete.bin|20", "READY")
            dropped.sendall(b"short")
            dropped.shutdown(socket.SHUT_WR)
            assert dropped.recv(1) == b""


def test_cli(port, directory):
    payload = (bytes(range(256)) * 8192) + b"\x00\n\xff"
    (directory / "input.bin").write_bytes(payload)
    (directory / "empty.bin").touch()
    (directory / "existing.bin").write_bytes(b"keep")
    result = subprocess.run(
        [str(ROOT / "build/client"), "127.0.0.1", str(port)],
        input=(
            "LOGIN|member|second\nLIST_MEMBERS|1\n"
            "put 1 empty.bin zero.bin\nget 1 zero.bin empty-copy.bin\n"
            "put 1 input.bin payload.bin\nput 1 input.bin payload.bin\n"
            "get 1 missing.bin missing-copy.bin\n"
            "get 1 payload.bin existing.bin\nget 1 payload.bin output.bin\n"
            "LIST_FILES|1|.\nPING\nQUIT\n"
        ),
        cwd=directory, capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    replies = result.stdout.splitlines()
    assert replies[:6] == [
        "OK|CONNECTED|SyncSpace", "OK|LOGIN|2", "OK|MEMBERS|2",
        "MEMBER|1|2", "MEMBER|2|1", "END",
    ], replies
    assert replies.count("OK|UPLOAD") == 2, replies
    assert "ERR|EXISTS" in replies and "ERR|NOT_FOUND" in replies, replies
    assert replies[-3:] == ["END", "OK|PONG", "OK|BYE"], replies
    assert (directory / "output.bin").read_bytes() == payload
    assert (directory / "empty-copy.bin").read_bytes() == b""
    assert (directory / "existing.bin").read_bytes() == b"keep"
    assert not (directory / "missing-copy.bin").exists()


def main():
    # Preserve the old group-create rollback regression in isolated storage.
    with TemporaryDirectory() as rollback_tmp:
        rollback_dir = Path(rollback_tmp)
        with launch_server(rollback_dir) as (port, connect):
            (rollback_dir / "storage/groups").touch()
            with connect() as sock:
                request(sock, "REGISTER|probe|pass", "OK|REGISTER|1")
                request(sock, "LOGIN|probe|pass", "OK|LOGIN|1")
                request(sock, "CREATE_GROUP|rollback", "ERR|IO")
                request(sock, "LIST_GROUPS", "OK|GROUPS|0")
                assert line(sock) == b"END\n"
            (rollback_dir / "storage/groups").unlink()

    with TemporaryDirectory() as temporary:
        directory = Path(temporary)
        with launch_server(directory) as (port, connect):
            test_sessions(connect)
            test_cli(port, directory)
        group_dir = directory / "storage/groups/1"
        assert (group_dir / "first.bin").read_bytes() == b"A" * 65543
        assert (group_dir / "second.bin").read_bytes() == b"B" * 65543
        assert not (group_dir / "incomplete.bin").exists()
        assert not list(group_dir.glob("*.part"))
        assert (directory / "storage/syncspace.db").is_file()

        # A real restart in the SAME directory must keep account IDs,
        # approved group membership, pending requests and uploaded bytes.
        with launch_server(directory) as (port, connect):
            with connect() as leader, connect() as member, connect() as extra:
                request(leader, "LOGIN|leader|first pass", "OK|LOGIN|1")
                request(member, "LOGIN|member|second", "OK|LOGIN|2")
                request(member, "LIST_GROUPS", "OK|GROUPS|1")
                assert [line(member), line(member)] == [b"GROUP|1|team|1\n", b"END\n"]
                request(member, "LIST_MEMBERS|1", "OK|MEMBERS|2")
                assert [line(member), line(member), line(member)] == [
                    b"MEMBER|1|2\n", b"MEMBER|2|1\n", b"END\n"]
                request(member, "REQUEST_JOIN|1", "ERR|EXISTS")
                request(member, "DOWNLOAD|1|first.bin", "FILE|65543")
                data = b""
                while len(data) < 65543:
                    fragment = member.recv(65543 - len(data))
                    assert fragment
                    data += fragment
                assert data == b"A" * 65543
                request(extra, "REGISTER|third|pass", "OK|REGISTER|3")
                request(extra, "LOGIN|third|pass", "OK|LOGIN|3")
                request(extra, "REQUEST_JOIN|1", "OK|REQUESTED")

        with launch_server(directory) as (port, connect):
            with connect() as leader, connect() as extra:
                request(leader, "LOGIN|leader|first pass", "OK|LOGIN|1")
                request(extra, "LOGIN|third|pass", "OK|LOGIN|3")
                request(leader, "LIST_REQUESTS|1", "OK|REQUESTS|1")
                assert [line(leader), line(leader)] == [b"REQUEST|3\n", b"END\n"]
                request(leader, "APPROVE_JOIN|1|3", "OK|APPROVED")
                request(extra, "LIST_GROUPS", "OK|GROUPS|1")
                assert [line(extra), line(extra)] == [b"GROUP|1|team|1\n", b"END\n"]
    print("WEEK 2 E2E + RESTART PERSISTENCE TEST PASS")


if __name__ == "__main__":
    main()
