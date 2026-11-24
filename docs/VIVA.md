# Viva preparation

1. Why a thread pool? Four workers bound creation overhead and expensive hashing.
   Many sockets are multiplexed by one network thread.
2. Concurrency versus parallelism? Clients make interleaved progress; workers may
   run simultaneously on different cores.
3. Why poll and nonblocking? Readiness avoids blocking on idle peers. EAGAIN still
   needs handling even after a readiness notification.
4. Why framing? TCP preserves ordered bytes, not application messages. Buffers
   reconstruct fragmented/coalesced LF-delimited lines.
5. Why track partial sends? send may accept fewer bytes than requested. Advance
   by the return value and resume remaining bytes on POLLOUT.
6. What is a critical section? Shared queue updates protected by a mutex.
7. Mutex versus semaphore? Mutex protects invariants; semaphore counts available
   connection permits. Each registered client owns exactly one permit.
8. Why condition predicates? Workers sleep instead of spinning; while loops
   account for spurious wakeups and state changes before reacquiring the mutex.
9. What is unfinished work? Accepted tasks whose results are not consumed/discarded.
10. Why can shutdown deadlock? Workers can wait for result capacity while main
    joins them. Drain results or wake publishers in discard mode before joining.
11. Why stable IDs? Slots and descriptor numbers are reused; IDs prevent stale
    results changing replacement sessions.
12. How is order preserved? One in-flight task per connection; apply its result
    before submitting the next line. Different clients still run concurrently.
13. Why no client mutex? Main owns client and room state exclusively. Workers
    receive copies, not mutable session pointers.
14. Why hash outside the auth mutex? Hashing is expensive; only account lookup,
    update, and snapshot persistence serialize.
15. Why Argon2id? Salted memory-hard password hashing raises guessing cost.
    libsodium supplies the implementation and encoded hashes.
16. Is traffic encrypted? No. Stored hashes are protected; ordinary TCP carries
    plaintext. TLS is future work.
17. What prevents impersonation? Verified login assigns a server-controlled
    username; message input cannot choose a sender identity.
18. Why monotonic time? Wall-clock changes must not bypass/refill rate limits.
    IP auth limits survive reconnects, but reset on server restart.
19. What can a signal handler do? Here it sets sig_atomic_t only. Main performs
    cleanup; no printf, malloc, or mutex operations occur in handlers.
20. Why atomic file replacement? A completed snapshot replaces the old account
    file, avoiding partially written records. flock prevents conflicting processes.
21. What is backpressure? Pause reads when input is full. Disconnect an overflowing
    output recipient instead of blocking everyone's messages.
22. What remains unproven? Native Linux compilation, runtime tests, sanitizers and
    measurements are pending; no test proves every possible schedule safe.

Trace one message through main, receive_input, dispatch_one, worker_function,
process_completed_tasks, apply_result, and flush_output. Explain how a disconnect
only changes result lookup eligibility, never gives a worker ownership of a socket.
