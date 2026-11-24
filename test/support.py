"""Test-only clients and isolated server lifecycle; never touches real accounts."""
from pathlib import Path
import os
import select
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT = 30 if os.environ.get("CHAT_VALGRIND") else 10

class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT)
        self.buffer = b""
        greeting = self.line()
        if not greeting.startswith("OK welcome"):
            self.close()
            raise RuntimeError(greeting)

    def send(self, text):
        self.socket.sendall(text.encode("ascii"))

    def line(self, timeout=TIMEOUT):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.socket], [], [], remaining)[0]:
                raise TimeoutError("Expected a server response")
            chunk = self.socket.recv(4096)
            if not chunk:
                raise EOFError("Server closed connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode("ascii")

    def request(self, text):
        self.send(text + "\n")
        return self.line()

    def quiet(self, timeout=0.15):
        return not self.buffer and not select.select([self.socket], [], [], timeout)[0]

    def close(self):
        self.socket.close()

class RunningServer:
    def __init__(self, directory=None):
        self.temporary = tempfile.TemporaryDirectory(prefix="chat-test-") if directory is None else None
        self.directory = Path(self.temporary.name if self.temporary else directory)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        self.error_file = open(self.directory / "test-stderr.txt", "w+")
        command = [str(ROOT / "server_app"), "127.0.0.1", str(self.port)]
        if os.environ.get("CHAT_VALGRIND"):
            command = ["valgrind", "--leak-check=full", "--show-leak-kinds=all",
                       "--errors-for-leak-kinds=definite,indirect", "--error-exitcode=99", *command]
        self.process = subprocess.Popen(command, cwd=self.directory,
                                        stdout=subprocess.DEVNULL, stderr=self.error_file)
        deadline = time.monotonic() + TIMEOUT
        try:
            while True:
                try:
                    peer = self.peer()
                    peer.close()
                    break
                except (OSError, EOFError, RuntimeError):
                    if self.process.poll() is not None or time.monotonic() > deadline:
                        self.error_file.flush()
                        raise RuntimeError((self.directory / "test-stderr.txt").read_text())
                    time.sleep(0.03)
        except BaseException:
            self.close()
            raise

    def peer(self):
        return Peer(self.port)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=TIMEOUT + 10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError("Server shutdown timed out")
        self.error_file.flush()
        diagnostics = (self.directory / "test-stderr.txt").read_text()
        if self.process.returncode != 0:
            raise AssertionError(f"Server exit {self.process.returncode}: {diagnostics}")
        if "ERROR: AddressSanitizer" in diagnostics or "runtime error:" in diagnostics or "WARNING: ThreadSanitizer" in diagnostics:
            raise AssertionError(diagnostics)

    def close(self):
        try:
            self.stop()
        finally:
            self.error_file.close()
            if self.temporary:
                self.temporary.cleanup()
