"""Local integration test for the TCP client and server (Python 3)."""

from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def line(sock):
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(1)
        assert chunk, f"Unexpected EOF: {data!r}"
        data += chunk
        assert len(data) < 2048
    return data


def main():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    server = subprocess.Popen(
        [str(ROOT / "build/server"), str(port)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )

    def connect():
        sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        assert line(sock) == b"OK CONNECTED SyncSpace\n"
        return sock

    try:
        deadline = time.monotonic() + 5
        while True:
            assert server.poll() is None, "Server exited during startup"
            try:
                with connect():
                    break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.02)

        result = subprocess.run(
            [str(ROOT / "build/client"), "127.0.0.1", str(port)],
            input="PING\nhello\nQUIT", capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 0, result.stderr
        for response in ("OK CONNECTED SyncSpace\n", "OK PONG\n",
                         "OK ECHO hello\n", "OK BYE\n"):
            assert response in result.stdout, result.stdout

        with connect() as first, connect() as second:
            first.sendall(b"PI")
            second.sendall(b"PING\n")
            assert line(second) == b"OK PONG\n"
            first.sendall(b"NG\nhello\nQUIT\n")
            for expected in (b"OK PONG\n", b"OK ECHO hello\n", b"OK BYE\n"):
                assert line(first) == expected
            assert first.recv(1) == b""

        for payload in (b"x" * 1023, b"bad\0line\n", b"unfinished"):
            with connect() as sock:
                sock.sendall(payload)
                sock.shutdown(socket.SHUT_WR)
                assert line(sock) == b"ERR Invalid or oversized line\n"

        with connect() as sock:
            sock.sendall(b"PING\nQUIT\n")
            assert line(sock) == b"OK PONG\n"
            assert line(sock) == b"OK BYE\n"
        print("CONNECTION TEST PASS")
    finally:
        server.terminate()
        try:
            server.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate(timeout=3)


if __name__ == "__main__":
    main()
