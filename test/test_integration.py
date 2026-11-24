"""Linux integration tests against native C server and client."""
import os
import signal
import socket
import stat
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from support import ROOT, TIMEOUT, Peer, RunningServer

PASSWORD = "testStrongPass1"

class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.server = RunningServer()
        self.addCleanup(self.server.close)

    def peer(self):
        peer = self.server.peer()
        self.addCleanup(peer.close)
        return peer

    def account(self, name):
        peer = self.peer()
        self.assertTrue(peer.request(f"/register {name} {PASSWORD}").startswith("OK registered"))
        self.assertTrue(peer.request(f"/login {name} {PASSWORD}").startswith("OK logged_in"))
        return peer

    def test_auth_ordering_and_framing(self):
        peer = self.peer()
        self.assertEqual(peer.request("unauthorized"), "ERR login_required")
        peer.send("/reg")
        peer.send(f"ister alice {PASSWORD}\n/login alice {PASSWORD}\n/create study\nhello\n")
        self.assertTrue(peer.line().startswith("OK registered"))
        self.assertTrue(peer.line().startswith("OK logged_in"))
        self.assertEqual(peer.line(), "OK room_created_and_joined")
        self.assertEqual(peer.line(), "MSG study alice hello")
        self.assertEqual(peer.request("/quit"), "BYE")

    def test_auth_failures_duplicate_and_policy(self):
        alice = self.account("alice")
        bob = self.peer()
        self.assertEqual(bob.request(f"/register alice {PASSWORD}"), "ERR username_unavailable")
        self.assertEqual(bob.request("/login alice wrongPass1"), "ERR credentials")
        self.assertEqual(bob.request(f"/login alice {PASSWORD}"), "ERR already_online")
        self.assertEqual(bob.request("/register invalid/name password1"), "ERR name_or_password_policy")
        self.assertEqual(alice.request("/logout"), "OK logged_out")
        self.assertTrue(bob.request(f"/login alice {PASSWORD}").startswith("OK logged_in"))
        self.assertEqual(alice.request("/users"), "ERR login_required")

    def test_room_and_private_isolation(self):
        alice, bob, charlie = [self.account(name) for name in ("alice", "bob", "charlie")]
        self.assertEqual(alice.request("public"), "MSG general alice public")
        self.assertEqual(bob.line(), "MSG general alice public")
        self.assertEqual(charlie.line(), "MSG general alice public")
        self.assertEqual(alice.request("/msg bob secret"), "OK private_sent")
        self.assertEqual(bob.line(), "PRIVATE alice secret")
        self.assertTrue(charlie.quiet())
        self.assertEqual(alice.request("/create study"), "OK room_created_and_joined")
        self.assertEqual(bob.request("/join study"), "OK joined")
        self.assertEqual(alice.request("isolated"), "MSG study alice isolated")
        self.assertEqual(bob.line(), "MSG study alice isolated")
        self.assertTrue(charlie.quiet())
        self.assertEqual(bob.request("/leave"), "OK left_room")
        self.assertEqual(bob.request("no room"), "ERR join_room_first")
        self.assertEqual(bob.request("/join missing"), "ERR room_not_found")
        bob.close()
        # A new request after closing allows the event loop to remove membership.
        time.sleep(0.1)
        self.assertEqual(alice.request("/rooms"), "OK rooms")
        rooms = []
        while (line := alice.line()) != "END rooms":
            rooms.append(line)
        self.assertIn("ROOM study 1", rooms)
        self.assertEqual(alice.request("/users"), "OK users")
        users = []
        while (line := alice.line()) != "END users":
            users.append(line)
        self.assertEqual(set(users), {"USER alice", "USER charlie"})
        log = (self.server.directory / "logs/server.log").read_text()
        self.assertNotIn(PASSWORD, log)
        self.assertNotIn("secret", log)

    def test_persistence_and_permissions(self):
        alice = self.account("alice")
        alice.close()
        self.server.stop()
        database = self.server.directory / "data/users.db"
        stored = database.read_text()
        self.assertIn("$argon2id$", stored)
        self.assertNotIn(PASSWORD, stored)
        self.assertEqual(stat.S_IMODE(database.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(database.parent.stat().st_mode), 0o700)
        restarted = RunningServer(self.server.directory)
        try:
            peer = restarted.peer()
            try:
                self.assertTrue(peer.request(f"/login alice {PASSWORD}").startswith("OK logged_in"))
            finally:
                peer.close()
        finally:
            restarted.close()

    def test_input_limits_and_malformed_commands(self):
        for payload in (b"x" * 1024 + b"\n", b"bad\x00line\n", b"\x1b[31m\n"):
            peer = self.peer()
            peer.socket.sendall(payload)
            self.assertTrue(peer.line().startswith("ERR "))
            peer.close()
        peer = self.account("alice")
        self.assertEqual(peer.request("x" * 1023), "MSG general alice " + "x" * 1023)
        for command in ("/unknown", "/msg", "/login", ""):
            self.assertEqual(peer.request(command), "ERR invalid_command")
        self.assertEqual(peer.request("/help extra"), "ERR invalid_command")

    def test_capacity_and_slot_reuse(self):
        peers = [self.peer() for _ in range(20)]
        with socket.create_connection(("127.0.0.1", self.server.port), timeout=TIMEOUT) as extra:
            self.assertTrue(extra.recv(1024).startswith(b"ERR capacity"))
        peers[0].close()
        time.sleep(0.15)
        replacement = self.peer()
        self.assertTrue(replacement.request("/help").startswith("OK commands"))

    def test_auth_throttle_survives_reconnection(self):
        # Invalid short passwords avoid expensive hashing while exercising the
        # same IP throttle. New sockets do not receive a fresh auth allowance.
        for _ in range(12):
            peer = self.peer()
            self.assertEqual(peer.request("/login nobody bad"), "ERR credentials")
            peer.close()
        peer = self.peer()
        self.assertEqual(peer.request("/login nobody bad"), "ERR auth_rate")

    def test_message_rate_and_fairness(self):
        flood = self.peer()
        observer = self.peer()
        flood.send("/help\n" * 60)
        lines = []
        while not lines or not lines[-1].startswith("ERR message_rate"):
            lines.append(flood.line())
        self.assertLessEqual(len(lines), 60)
        self.assertTrue(observer.request("/help").startswith("OK commands"))

    def test_native_client_and_eof(self):
        result = subprocess.run([str(ROOT / "client_app"), "127.0.0.1", str(self.server.port)],
                                input="/help\n/quit\n", text=True, capture_output=True,
                                timeout=TIMEOUT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("OK commands", result.stdout)
        self.assertIn("BYE", result.stdout)
        peer = self.peer()
        peer.send("/help\n")
        peer.socket.shutdown(socket.SHUT_WR)
        self.assertTrue(peer.line().startswith("OK commands"))
        self.assertEqual(peer.line(), "BYE")

    def test_disconnect_stale_results_and_shutdown(self):
        for _ in range(10):
            peer = self.peer()
            peer.send("/help\n")
            peer.close()
        peer = self.peer()
        self.assertEqual(peer.request("/users"), "ERR login_required")
        peer.send("/help\n" * 5)
        self.server.process.send_signal(signal.SIGINT)
        self.server.stop()

    def test_shutdown_during_authentication(self):
        peers = [self.peer() for _ in range(4)]
        for index, peer in enumerate(peers):
            peer.send(f"/register pending{index} {PASSWORD}\n")
        time.sleep(0.1)
        self.server.process.send_signal(signal.SIGTERM)
        self.server.stop()

    def test_incomplete_eof(self):
        peer = self.peer()
        peer.send("unfinished")
        peer.socket.shutdown(socket.SHUT_WR)
        self.assertEqual(peer.line(), "ERR incomplete_line")

    def test_second_server_cannot_open_same_accounts(self):
        result = subprocess.run([str(ROOT / "server_app"), "127.0.0.1", str(self.server.port)],
                                cwd=self.server.directory, capture_output=True,
                                timeout=TIMEOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Account storage unavailable", result.stderr)

if __name__ == "__main__":
    unittest.main()
