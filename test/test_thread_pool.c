#include "thread_pool.h"
#include "security.h"
#include <assert.h>
#include <sodium.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

typedef struct { sem_t gate; atomic_int entered; int gated; } Context;
static void pause_briefly(void)
{
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
}
static void process(const Task *task, Result *result, void *argument)
{
    Context *context = argument;
    atomic_fetch_add(&context->entered, 1);
    if (context->gated)
        assert(sem_wait(&context->gate) == 0);
    result->sender_id = task->sender_id;
    result->status = 42;
}
static void wait_entered(Context *context, int count)
{
    double deadline = monotonic_now() + 10;
    while (atomic_load(&context->entered) < count) {
        assert(monotonic_now() < deadline);
        pause_briefly();
    }
}
int main(void)
{
    assert(sodium_init() >= 0);
    ThreadPool pool;
    Context context = {.gated = 1};
    atomic_init(&context.entered, 0);
    assert(sem_init(&context.gate, 0, 0) == 0);
    assert(thread_pool_init(&pool, process, &context) == 0);
    Task task = {0};
    for (int i = 0; i < WORKER_COUNT; ++i) {
        task.sender_id = (uint64_t)i;
        assert(thread_pool_submit(&pool, &task));
    }
    wait_entered(&context, WORKER_COUNT);
    for (int i = 0; i < QUEUE_SIZE; ++i) {
        task.sender_id = (uint64_t)(WORKER_COUNT + i);
        assert(thread_pool_submit(&pool, &task));
    }
    assert(!thread_pool_submit(&pool, &task));
    assert(thread_pool_unfinished(&pool) == QUEUE_SIZE + WORKER_COUNT);
    thread_pool_stop_accepting(&pool);
    assert(!thread_pool_submit(&pool, &task));
    for (int i = 0; i < QUEUE_SIZE + WORKER_COUNT; ++i)
        assert(sem_post(&context.gate) == 0);
    int seen[QUEUE_SIZE + WORKER_COUNT] = {0};
    int consumed = 0;
    double deadline = monotonic_now() + 10;
    Result result;
    while (consumed < QUEUE_SIZE + WORKER_COUNT) {
        if (thread_pool_get_result(&pool, &result)) {
            assert(result.status == 42 && result.sender_id < QUEUE_SIZE + WORKER_COUNT);
            assert(!seen[result.sender_id]++);
            ++consumed;
        } else {
            assert(monotonic_now() < deadline);
            pause_briefly();
        }
    }
    assert(thread_pool_unfinished(&pool) == 0);
    thread_pool_destroy(&pool);
    assert(sem_destroy(&context.gate) == 0);

    context.gated = 0;
    atomic_store(&context.entered, 0);
    assert(thread_pool_init(&pool, process, &context) == 0);
    for (int i = 0; i < QUEUE_SIZE; ++i)
        assert(thread_pool_submit(&pool, &task));
    deadline = monotonic_now() + 10;
    for (;;) {
        pthread_mutex_lock(&pool.mutex);
        size_t count = pool.result_count;
        pthread_mutex_unlock(&pool.mutex);
        if (count == QUEUE_SIZE)
            break;
        assert(monotonic_now() < deadline);
        pause_briefly();
    }
    for (int i = 0; i < WORKER_COUNT; ++i)
        assert(thread_pool_submit(&pool, &task));
    wait_entered(&context, QUEUE_SIZE + WORKER_COUNT);
    /* Full completed queue and workers trying to publish: must not deadlock. */
    thread_pool_destroy(&pool);
    puts("PASS pool concurrency, saturation, accounting, and destruction");
    return 0;
}
