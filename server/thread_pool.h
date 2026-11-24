#ifndef CHAT_THREAD_POOL_H
#define CHAT_THREAD_POOL_H
#include "protocol.h"
#include <pthread.h>
#include <stdint.h>
typedef struct { uint64_t sender_id; char message[BUFFER_SIZE]; } Task;
typedef struct { uint64_t sender_id; Command command; int status; } Result;
typedef void (*TaskProcessor)(const Task *, Result *, void *);
typedef struct {
    pthread_t threads[WORKER_COUNT];
    Task pending[QUEUE_SIZE];
    Result completed[QUEUE_SIZE];
    size_t task_front, task_count, result_front, result_count;
    size_t unfinished_tasks, active_workers;
    int accepting, stopping, discarding;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty, result_not_full, all_done;
    TaskProcessor process;
    void *context;
} ThreadPool;
int thread_pool_init(ThreadPool *pool, TaskProcessor process, void *context);
int thread_pool_submit(ThreadPool *pool, const Task *task);
int thread_pool_get_result(ThreadPool *pool, Result *result);
size_t thread_pool_unfinished(ThreadPool *pool);
void thread_pool_stop_accepting(ThreadPool *pool);
/* Safe even with a full result queue: destroy discards unconsumed results. */
void thread_pool_destroy(ThreadPool *pool);
#endif
