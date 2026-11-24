# Security design and limitations

## Authentication

libsodium is initialized before workers. crypto_pwhash_str_alg explicitly selects
Argon2id13 with interactive limits and random salts; crypto_pwhash_str_verify
checks passwords. No custom crypto or fast SHA password storage. Four workers
bound simultaneous hashing. A dummy hash reduces obvious unknown-user timing
signals, but username availability/duplicate registration remain observable.

Registration does not authenticate. Login sets a server-owned identity and joins
general. All chat/listing/room operations require authentication. Duplicate online
logins are rejected. Names are case-sensitive; clients cannot choose sender labels.

Data directory is 0700, files 0600. Ownership, type, symlink/hardlink checks reject
unsafe storage. A lifetime flock prevents concurrent server processes updating
accounts. Persistence writes an exclusive temporary snapshot, flushes/fsyncs,
and atomically renames it. Directory fsync is attempted; failure after rename
cannot be rolled back, so absolute power-loss durability is not promised.
Startup under the lock removes a crash-leftover temporary snapshot. Malformed,
duplicate, or unsupported records fail startup. Current interactive hash parameters
are required; hash-parameter migration is future work.

Temporary task/input/password/hash buffers are cleared where practical with
sodium_memzero or volatile client wipes. Interactive /login NAME and /register NAME
hide the next password line. Full wire forms remain visibly typed if used in a
terminal. Memory is not locked; swap/core dumps and hostile same-user processes
are outside this project's protections. Use disposable passwords.

## Validation and limits

Only printable ASCII plus LF is accepted. NUL, CR, terminal escapes, other controls,
and non-ASCII are rejected before dispatch. Lines max 1023 bytes excluding LF.
Names max 31 letters/digits/underscores; passwords 8–128 printable non-space bytes.
Malformed commands, missing/extra arguments, and invalid room operations get errors.

| Resource/policy | Limit |
| --- | --- |
| Connections / workers | 20 / 4 |
| Pending / completed queue | 100 each |
| Per-client in-flight tasks | 1 |
| Per-client input / output | 8192 bytes each |
| Request bucket | Burst 20, refill 5/sec; excess closes connection |
| IP auth bucket | Burst 12, refill 1 per 5 sec; excess closes connection |
| IP connection bucket | Burst 40, refill 2/sec |
| IP records | 128; unreferenced inactive entries reusable after 600 sec |
| Unauthenticated lifetime | 60 sec from connect/logout |
| Buffered-input deadline | 15 sec since buffer became nonempty/last line consumed |
| Closing output | 2 sec |
| Accounts / rooms | 128 / 10 including general |

IP records survive reconnects until server restart; active records are not evicted.
Success does not reset auth limits. Table exhaustion rejects new IP addresses.
NAT/localhost clients share budgets; distributed attackers can still deny service.
The input deadline resets after each complete line, not per-byte timestamps.

Full task queues return ERR busy_retry. With one task per connected client, ordinary
server use cannot fill a 100-slot queue using 20 connections. Dedicated pool tests
exercise saturation and full-result destruction independently.

## Logging and transport

Logs contain constant labels, timestamps, severity and connection IDs, never raw
input, passwords, hashes, usernames, or message content. Main-thread logging avoids
write races. Logs are truncated above roughly 1 MiB, sacrificing history to bound
disk use. Reject counters are printed at shutdown instead of logging every attempt.

**Ordinary TCP is not encrypted.** Hashing protects files, not network traffic.
Keep the localhost default. No TLS, end-to-end encryption, certificate verification,
password reset, production audit, or comprehensive distributed abuse defence is
claimed. Future transport security should use a tested standard TLS library.
