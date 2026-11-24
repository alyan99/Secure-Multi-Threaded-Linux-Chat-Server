#include "thread_pool.h"
#include <sodium.h>
#include <string.h>

static void *worker_function(void *argument)
{
    ThreadPool *pool = argument;
    for (;;) {
        Task task;
        Result result = {0};
        pthread_mutex_lock(&pool->mutex);
        while (!pool->task_count && !pool->stopping)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        if (!pool->task_count && pool->stopping) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        task = pool->pending[pool->task_front];
        sodium_memzero(&pool->pending[pool->task_front], sizeof(Task));
        pool->task_front = (pool->task_front + 1) % QUEUE_SIZE;
        --pool->task_count;
        ++pool->active_workers;
        pthread_mutex_unlock(&pool->mutex);

        result.sender_id = task.sender_id;
        pool->process(&task, &result, pool->context);
        sodium_memzero(&task, sizeof(task));

        pthread_mutex_lock(&pool->mutex);
        --pool->active_workers;
        while (pool->result_count == QUEUE_SIZE && !pool->discarding)
            pthread_cond_wait(&pool->result_not_full, &pool->mutex);
        if (pool->discarding) {
            --pool->unfinished_tasks;
        } else {
            size_t rear = (pool->result_front + pool->result_count) % QUEUE_SIZE;
            pool->completed[rear] = result;
            ++pool->result_count;
        }
        if (!pool->unfinished_tasks)
            pthread_cond_broadcast(&pool->all_done);
        pthread_mutex_unlock(&pool->mutex);
        sodium_memzero(&result, sizeof(result));
    }
    return NULL;
}
int thread_pool_init(ThreadPool *pool, TaskProcessor process, void *context)
{
    memset(pool, 0, sizeof(*pool));
    if (!process || pthread_mutex_init(&pool->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&pool->not_empty, NULL) != 0)
        goto mutex_fail;
    if (pthread_cond_init(&pool->result_not_full, NULL) != 0)
        goto empty_fail;
    if (pthread_cond_init(&pool->all_done, NULL) != 0)
        goto full_fail;
    pool->process = process;
    pool->context = context;
    pool->accepting = 1;
    int created = 0;
    for (; created < WORKER_COUNT; ++created)
        if (pthread_create(&pool->threads[created], NULL, worker_function, pool) != 0)
            break;
    if (created == WORKER_COUNT)
        return 0;
    pthread_mutex_lock(&pool->mutex);
    pool->stopping = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
    for (int i = 0; i < created; ++i)
        pthread_join(pool->threads[i], NULL);
    pthread_cond_destroy(&pool->all_done);
full_fail:
    pthread_cond_destroy(&pool->result_not_full);
empty_fail:
    pthread_cond_destroy(&pool->not_empty);
mutex_fail:
    pthread_mutex_destroy(&pool->mutex);
    return -1;
}
int thread_pool_submit(ThreadPool *pool, const Task *task)
{
    int accepted = 0;
    pthread_mutex_lock(&pool->mutex);
    if (pool->accepting && pool->task_count < QUEUE_SIZE) {
        size_t rear = (pool->task_front + pool->task_count) % QUEUE_SIZE;
        pool->pending[rear] = *task;
        ++pool->task_count;
        ++pool->unfinished_tasks;
        accepted = 1;
        pthread_cond_signal(&pool->not_empty);
    }
    pthread_mutex_unlock(&pool->mutex);
    return accepted;
}
int thread_pool_get_result(ThreadPool *pool, Result *result)
{
    pthread_mutex_lock(&pool->mutex);
    if (!pool->result_count) {
        pthread_mutex_unlock(&pool->mutex);
        return 0;
    }
    *result = pool->completed[pool->result_front];
    sodium_memzero(&pool->completed[pool->result_front], sizeof(Result));
    pool->result_front = (pool->result_front + 1) % QUEUE_SIZE;
    --pool->result_count;
    --pool->unfinished_tasks;
    pthread_cond_signal(&pool->result_not_full);
    if (!pool->unfinished_tasks)
        pthread_cond_broadcast(&pool->all_done);
    pthread_mutex_unlock(&pool->mutex);
    return 1;
}
size_t thread_pool_unfinished(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutex);
    size_t count = pool->unfinished_tasks;
    pthread_mutex_unlock(&pool->mutex);
    return count;
}
void thread_pool_stop_accepting(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutex);
    pool->accepting = 0;
    pthread_mutex_unlock(&pool->mutex);
}
void thread_pool_destroy(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutex);
    pool->accepting = 0;
    pool->stopping = pool->discarding = 1;
    pool->unfinished_tasks -= pool->result_count;
    pool->result_count = 0;
    sodium_memzero(pool->completed, sizeof(pool->completed));
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->result_not_full);
    pthread_mutex_unlock(&pool->mutex);
    for (int i = 0; i < WORKER_COUNT; ++i)
        pthread_join(pool->threads[i], NULL);
    pthread_cond_destroy(&pool->all_done);
    pthread_cond_destroy(&pool->result_not_full);
    pthread_cond_destroy(&pool->not_empty);
    pthread_mutex_destroy(&pool->mutex);
    sodium_memzero(pool, sizeof(*pool));
}
