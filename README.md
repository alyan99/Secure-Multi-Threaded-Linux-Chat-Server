# Secure Multi-Threaded Linux Chat Server in C

Native Linux Operating Systems project for Kali Linux and Linux Mint.
Implementation for Phases 1�9 is present. **Linux compilation, runtime acceptance,
sanitizers, and performance measurements are pending your VM verification.**

## Features

- Nonblocking IPv4 TCP server, poll(), 20 connections, four persistent POSIX workers.
- Bounded task/result queues, mutexes, conditions, unfinished-task accounting.
- Semaphore connection permits; bounded input/output and partial TCP reads/writes.
- Per-client ordering, stable IDs, slow-recipient removal, controlled signal shutdown.
- libsodium Argon2id registration/login, atomic account snapshots, owner-only files.
- Authenticated room/public/private messages and online-user/room listings.
- Monotonic message, IP authentication, and IP connection limits; input timeouts.
- Safe timestamped logs and an asynchronous client with hidden password prompts.
- C module/concurrency/network-buffer tests and Python integration/load tests.

## Transfer, build, run

Zip this folder including dotfiles, extract into `~/OS_Project` in Kali, and run
from the folder containing the Makefile. See [setup](docs/SETUP.md) for dependencies.

```bash
make
make test
```

One terminal: `./server_app`. Three other terminals: `./client_app`.
Default: `127.0.0.1:8080`. Both accept `IPv4 PORT`, for example
`./server_app 127.0.0.1 9090` and `./client_app 127.0.0.1 9090`.
Both support `--help` and `--version`.

## Commands

| Command | Behavior |
| --- | --- |
| `/register alice` | Native interactive client prompts for hidden password |
| `/login alice` | Native interactive client prompts for hidden password |
| `/register alice password` | Wire form for scripts; visibly typed in terminals |
| `/login alice password` | Wire form; authenticate and join general |
| `/logout` | Clear session and membership; keep connection |
| `hello everyone` | Message current room, including sender |
| `/msg bob hello` | Only bob receives content; sender receives acknowledgement |
| `/create study` | Create and join a room |
| `/join study` | Switch to an existing room |
| `/leave` | Leave room; private messaging and listings remain available |
| `/rooms` | List rooms and membership counts |
| `/users` | List authenticated online users |
| `/help` | List commands; available before login |
| `/quit` | Send BYE and disconnect |

Registration does not log in automatically. Names are case-sensitive: 1�31 ASCII
letters/digits/underscores. Passwords: 8�128 printable ASCII characters without
spaces. Lines: 1�1023 printable ASCII bytes followed by LF. CRLF, NUL, controls,
terminal escapes, empty lines, and non-ASCII are rejected. Prefer the native client
to Telnet, which can send CRLF/control bytes. Duplicate online logins are rejected.

Each user has one active room; rooms last until restart. Limits: 10 rooms including
general, 128 stored accounts, 20 connections. Room size is bounded by total clients.

## Build and test targets

`make server`, `make client`, `make debug`, `make test`, `make load`, `make asan`,
`make tsan`, `make valgrind`, `make clean`.
Run sanitizers separately. Clean before changing compiler flags; debug and
instrumentation targets do so automatically. Valgrind checks C tests and integration
servers. Load prints real JSON for twenty clients issuing /help; no performance
numbers are claimed yet. See [testing](docs/TESTING.md).

## Source map

- `server/`: network loop, pool, auth, security, chat, logger.
- `client/`: native terminal client.
- `include/`: shared config/protocol.
- `test/`: C tests, Python network/load suites.
- `data/`, `logs/`: runtime account storage and logs, ignored by Git.
- `docs/`: [setup](docs/SETUP.md), [architecture](docs/ARCHITECTURE.md),
  [security](docs/SECURITY.md), [testing](docs/TESTING.md), [viva](docs/VIVA.md),
  [demo](docs/DEMO.md), [phase report](docs/PHASES.md).

## Scope and limitations

**Ordinary TCP has no TLS or end-to-end encryption.** Hashing protects stored
credentials only. Use disposable demonstration passwords on localhost.
Four interactive hashes can require roughly 256 MiB plus process overhead. Allow
at least 1 GiB RAM in the VM; 2 GiB or more is preferable for development tools.
IP budgets group all localhost/NAT clients. No password reset, history, offline
messages, IPv6, room deletion, or multi-server coordination. Future work includes
tested TLS, policy configuration, and account migration. This is a university
project, not an audited production service.

Suggested commit: `feat: complete concurrent authenticated Linux chat implementation`.
No Git commit or remote push was performed automatically.
#
