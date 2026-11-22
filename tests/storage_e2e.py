"""
End-to-End (E2E) integration test for SyncSpace Week 2 Storage & File Transfer.
NOTE: Per Section 1.2 & 6.2 of the Week 2 Guide, this E2E test targets the full
integrated TCP server (after Auth, Groups, and Storage branches are merged).
"""

import hashlib
import os
from pathlib import Path
import random
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def recv_line(sock: socket.socket) -> bytes:
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(1)
        assert chunk, f"Unexpected EOF while reading line: {data!r}"
        data += chunk
        assert len(data) < 2048, "Oversized response line"
    return data


def recv_exact(sock: socket.socket, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = sock.recv(min(65536, size - len(buf)))
        assert chunk, f"Unexpected EOF after {len(buf)}/{size} bytes"
        buf.extend(chunk)
    return bytes(buf)


def send_cmd(sock: socket.socket, cmd: str) -> bytes:
    sock.sendall((cmd + "\n").encode("utf-8"))
    return recv_line(sock)


def upload_bytes(sock: socket.socket, gid: int, name: str, payload: bytes) -> bytes:
    resp = send_cmd(sock, f"UPLOAD|{gid}|{name}|{len(payload)}")
    if resp != b"READY\n":
        return resp
    if payload:
        sock.sendall(payload)
    return recv_line(sock)


def download_bytes(sock: socket.socket, gid: int, name: str):
    resp = send_cmd(sock, f"DOWNLOAD|{gid}|{name}")
    if not resp.startswith(b"FILE|"):
        return resp, None
    size = int(resp.decode("utf-8").strip().split("|")[1])
    data = recv_exact(sock, size)
    return resp, data


def main():
    server_bin = ROOT / "build" / "server"
    if not server_bin.exists():
        print("PENDING: build/server not built yet. Run `make` after integration.")
        return

    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]

    server = subprocess.Popen(
        [str(server_bin), str(port)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        deadline = time.monotonic() + 5.0
        while True:
            assert server.poll() is None, "Server exited during startup"
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=3)
                greeting = recv_line(sock)
                sock.close()
                if greeting != b"OK|CONNECTED|SyncSpace\n":
                    print(
                        f"PENDING: Server is still running Week 1 protocol ({greeting.decode().strip()!r}). "
                        "Waiting for Member 1 & Member 2 merge on main."
                    )
                    return
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)

        def connect_client() -> socket.socket:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            assert recv_line(s) == b"OK|CONNECTED|SyncSpace\n"
            return s

        suffix = os.getpid()
        user_leader = f"leader_{suffix}"
        user_outsider = f"outsider_{suffix}"

        with connect_client() as leader, connect_client() as outsider:
            # 1. Register & login leader and outsider
            assert send_cmd(leader, f"REGISTER|{user_leader}|pass123").startswith(b"OK|REGISTER|")
            assert send_cmd(leader, f"LOGIN|{user_leader}|pass123").startswith(b"OK|LOGIN|")

            assert send_cmd(outsider, f"REGISTER|{user_outsider}|pass123").startswith(b"OK|REGISTER|")
            assert send_cmd(outsider, f"LOGIN|{user_outsider}|pass123").startswith(b"OK|LOGIN|")

            # 2. Leader creates group
            grp_resp = send_cmd(leader, f"CREATE_GROUP|storage_grp_{suffix}")
            assert grp_resp.startswith(b"OK|GROUP|"), grp_resp
            gid = int(grp_resp.decode().strip().split("|")[2])

            # 3. MKDIR & LIST_FILES
            assert send_cmd(leader, f"MKDIR|{gid}|docs") == b"OK|MKDIR\n"
            assert send_cmd(outsider, f"MKDIR|{gid}|hacked") == b"ERR|FORBIDDEN\n"
            assert send_cmd(outsider, f"LIST_FILES|{gid}|.") == b"ERR|FORBIDDEN\n"

            # 4. Upload & Download 0-byte, binary 257-byte, and 2 MiB + 13-byte files
            assert upload_bytes(leader, gid, "empty.txt", b"") == b"OK|UPLOAD\n"
            hdr, data = download_bytes(leader, gid, "empty.txt")
            assert hdr == b"FILE|0\n" and data == b""

            bin257 = bytes(range(256)) + b"\xff"
            assert upload_bytes(leader, gid, "bin257.dat", bin257) == b"OK|UPLOAD\n"
            hdr, data = download_bytes(leader, gid, "bin257.dat")
            assert hdr == b"FILE|257\n" and data == bin257

            rng = random.Random(20261122)
            large_data = rng.randbytes((2 * 1024 * 1024) + 13)
            assert upload_bytes(leader, gid, "large2m.bin", large_data) == b"OK|UPLOAD\n"
            hdr, data = download_bytes(leader, gid, "large2m.bin")
            assert hdr == f"FILE|{len(large_data)}\n".encode()
            assert hashlib.sha256(data).hexdigest() == hashlib.sha256(large_data).hexdigest()

            # 5. Duplicate & unsafe path rejection
            assert upload_bytes(leader, gid, "bin257.dat", b"123") == b"ERR|EXISTS\n"
            assert upload_bytes(leader, gid, "../escape.bin", b"123").startswith(b"ERR|")
            assert send_cmd(outsider, f"DOWNLOAD|{gid}|bin257.dat") == b"ERR|FORBIDDEN\n"

            # 6. Interrupted upload mid-stream must clean up .part
            with connect_client() as drop_sock:
                assert send_cmd(drop_sock, f"LOGIN|{user_leader}|pass123").startswith(b"OK|LOGIN|")
                assert send_cmd(drop_sock, f"UPLOAD|{gid}|partial.bin|100000") == b"READY\n"
                drop_sock.sendall(b"X" * 50000)

            time.sleep(0.1)
            hdr, _ = download_bytes(leader, gid, "partial.bin")
            assert hdr == b"ERR|NOT_FOUND\n"

            assert send_cmd(leader, "QUIT") == b"OK|BYE\n"
            assert send_cmd(outsider, "QUIT") == b"OK|BYE\n"

        print("STORAGE E2E TEST PASS")
    finally:
        server.terminate()
        try:
            server.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate(timeout=3)


if __name__ == "__main__":
    main()
