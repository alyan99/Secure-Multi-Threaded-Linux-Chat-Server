# Kali Linux / Linux Mint setup

Zip `D:\OS_Project`, including dotfiles and data/logs .gitkeep files. Extract to
`~/OS_Project` on the VM's native Linux filesystem, rather than a Windows shared
folder, so permissions and file locks behave correctly. No Docker is needed.
Run commands from the project folder. Do not run the server as root.

## Check dependencies

```bash
uname -s
for tool in gcc make gdb valgrind strace git pkg-config python3; do
    command -v "$tool" || true
done
pkg-config --modversion libsodium
```

Install missing dependencies yourself if necessary:

```bash
sudo apt update
sudo apt install build-essential make gcc gdb valgrind strace git pkg-config libsodium-dev python3
```

No privileged installation was performed during preparation.

## Verify

```bash
make clean
make
make test
```

Expected: warning-free builds, six C executables report PASS, two Python suites
report OK. These are expectations, not recorded Linux results. Tests use temporary
accounts, not real data/users.db. Then run server/client in separate terminals
and follow [demo](DEMO.md). See [additional checks](TESTING.md).

## Troubleshooting

- Missing sodium.h/pkg-config: install libsodium-dev and pkg-config.
- Address in use: stop the old server or use port 9090 in both programs.
- Account storage error: check directory ownership, symlinks, malformed records,
  and whether another server holds the account lock. An existing unlocked
  users.lock file is normal; do not delete storage while the server is running.
- ERR auth_rate: wait for IP token refill. Reconnecting does not reset it.
- ERR message_rate: the flooding connection closes; reconnect with normal pacing.
- Terminal echo disabled after crash/SIGKILL: run `stty echo`.
- Unsupported sanitizer runtime/mapping: retain the error and report it; a failed
  runtime launch is not a passing race/memory test.

Optional local Git history after inspecting staged files:

```bash
git init
git add .
git status
git commit -m "feat: complete concurrent authenticated Linux chat implementation"
```

Check that binaries, logs, and hashes are excluded. Configure your own Git identity
if requested. A remote push is unnecessary for the VM demonstration.
