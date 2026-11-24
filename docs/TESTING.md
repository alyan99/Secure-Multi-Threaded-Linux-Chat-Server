# Tests and verification status

## Actual preparation checks

Windows host: GCC/Make unavailable. WSL lists only Docker Desktop's managed
distribution; it was not used and no Docker environment was introduced.
No privileged installation occurred.

Python test syntax, C grammar, source LF endings, local include resolution, and
Makefile wiring were checked. C grammar uses pycparser with stand-in system
typedefs and stripped includes. This does NOT perform C type checking/linking,
Linux execution, race detection, or leak detection. No runtime pass or performance
measurement is claimed. Final acceptance remains pending in Kali.

## Run on Linux

```bash
make clean
make
make test
make load
```

Tests use isolated temporary directories and disposable accounts. Python only
orchestrates the native C programs; it does not replace them.

| Test | Coverage |
| --- | --- |
| test_protocol.c | Parsing, arguments, unknown commands, lengths and controls |
| test_security.c | Validation, token exhaustion/refill/backwards time |
| test_chat.c | Default room, uniqueness, invalid names, maximum rooms |
| test_auth.c | Registration, duplicates, credentials, persistence, permissions |
| test_thread_pool.c | Four concurrent workers, saturation, counters, result uniqueness, stop accepting, full-result destruction |
| test_network_io.c | Actual server output helpers: socketpair partial sends, EAGAIN retention, output order, overflow |
| test_foundation.py | CLI help/version and invalid arguments |
| test_integration.py | Auth, fragmentation/coalescing, pipelined ordering, room/private isolation, cleanup, capacity/reuse, reconnect throttling, malformed input, half-close/EOF, native client, pending shutdown, process lock |
| test_load.py | Twenty concurrent clients, 200 unauthenticated /help requests, mean/P95 latency and throughput |

The socketpair test includes server.c with its main renamed to test real static
helpers without production test hooks. Disconnect tests exercise stale results,
not every possible thread schedule. Slow-recipient buffer behavior is tested
through the actual helper; this is not a WAN congestion benchmark.

## Instrumentation

```bash
make asan
make tsan
make valgrind
```

ASan/UBSan run together; TSan separately. Each target cleans/rebuilds. Valgrind
checks module tests and integration servers. The native-client subprocess is
not wrapped by that integration Valgrind mode; inspect it separately if needed.
Tests perform real Argon2id work, so provide enough RAM/time. Instrumentation
launch failures must be recorded, not described as passing results.
After instrumentation: `make clean` then `make`.

## Manual debugging and resource checks

```bash
ps -T -p $(pgrep -n server_app)
ss -ltnp | grep 8080
ps -p $(pgrep -n server_app) -o pid,nlwp,%cpu,rss,vsz
strace -f -e trace=network,poll,futex ./server_app 127.0.0.1 9090
gdb --args ./server_app 127.0.0.1 9090
tail -f logs/server.log
```

Expect five threads after initialization. GDB: run, info threads,
thread apply all bt, continue. Never retain credential-bearing traces from real
accounts; use disposable demonstration data.

## Performance recording

make load emits measured JSON; it has not run here. The workload measures parser,
pool and event-loop latency, not Argon2id or broadcast throughput. Record VM CPU/RAM,
compiler/libsodium versions, build flags, load JSON, and CPU/RSS readings. Shutdown
prints accepted/rejected connections and queue-busy events. No invented benchmark
numbers belong in the report. A clean sanitizer run covers only executed scenarios.

Acceptance: warning-free builds, passing tests, reviewed memory/race reports,
recorded measurements, and a manual three-client demo.
