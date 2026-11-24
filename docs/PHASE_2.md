> Historical snapshot. See [current phase status](PHASES.md) and [README](../README.md).

# Phase 2: basic TCP communication

## Phase 1 review

The folder structure, native C entry points, central configuration, Makefile,
ignore rules, smoke tests, and setup documentation are present. Linux environment
validation is still pending: GCC and Make are unavailable on the preparation
machine. Phase 1 cannot be marked fully verified. The user authorized proceeding
with Phase 2; both phases must be verified in Kali before claiming acceptance.

## Changes and responsibilities

`server/server.c` opens an IPv4 TCP listener, handles one connection at a time,
echoes complete lines, closes disconnected peers, and accepts replacements.
`client/client.c` connects, reads terminal lines, sends requests, prints responses,
and supports `/quit`, EOF, and interruption. Version is now 0.2.0.

Shared `protocol.c` defines `parse_endpoint()`, `read_line()`, and `send_all()`.
The Makefile links the protocol object into both executables and tracks its
header dependencies. CLI smoke tests were updated to avoid launching a server
that now intentionally stays running. New socket tests cover fragmentation,
coalesced lines, limits, control bytes, incomplete EOF, reconnect, the native
client, invalid endpoints, and SIGINT while a client is idle.

## OS concepts and system calls

`socket(AF_INET, SOCK_STREAM, 0)` creates an IPv4 TCP file descriptor.
`setsockopt(SO_REUSEADDR)` permits prompt listener restart; `bind()` assigns the
local address; `listen()` establishes a pending-connection backlog; `accept()`
returns a separate connected descriptor. The listener remains open between
sessions. `connect()` establishes the client side. `close()` releases owned
socket descriptors on both success and error paths.

TCP is a byte stream, so a single send need not match a single receive.
`read_line()` reads bytes until LF; this simple phase uses one-byte reads for
clarity, trading efficiency for straightforward framing. `send_all()` advances
by the actual count returned by `send()`. `MSG_NOSIGNAL` prevents a broken peer
from terminating the process with SIGPIPE. Malformed lines are not broadcast
or interpreted as commands.

`inet_pton()` validates numeric IPv4 text; `htons()` and `ntohs()` convert port
byte order. `<arpa/inet.h>` exposes these helpers, `<sys/socket.h>` exposes socket
APIs, and `<netinet/in.h>` defines IPv4 address structures. `<unistd.h>` provides
`close()`. `<errno.h>` reports failed system calls; `<stdlib.h>` provides checked
numeric conversion with `strtol()`.

`sigaction()` installs SIGINT/SIGTERM handlers. A handler only sets a
`volatile sig_atomic_t` flag; descriptor cleanup happens in normal code, keeping
the handler safe. Calls interrupted by a signal return to the caller instead of
retrying indefinitely. `poll()` with a 200 ms timeout lets the listener regularly
check the shutdown flag. This is not the multi-client event loop of Phase 3.
Accepted sockets use five-second receive/send timeouts (`<sys/time.h>`) to bound
individual blocking operations, including a rare signal-arrival race before I/O.

No worker threads or shared mutable queues exist, so no mutex or semaphore is
needed in this phase. Separate server/client processes communicate through the
kernel's TCP stack.

## Verification and phase status

Local checks: Python syntax, required file presence, source line endings, and
Makefile linkage/dependencies. Linux compilation and runtime suites have NOT
run; no GCC warnings, network behavior, or performance result is claimed.
Run `make clean && make && make test` in Kali, then manually use two terminals.
See README for exact commands and current restrictions.

Status: Phase 2 implemented, Linux acceptance pending. No confirmed runtime bugs;
blocking single-client behavior, ASCII-only framing, and per-operation rather
than total-line timeouts are documented limitations. No sanitizer or leak-check
result is available. Phase 3 requires nonblocking multi-client poll handling,
client identities, broadcast routing, and tests. Stop before that phase until
user approval.

Suggested commit: `feat: implement basic TCP client-server communication`.
