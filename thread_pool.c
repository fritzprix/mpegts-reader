#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include "gst_aplay.h"
#include "gplayer_defs.h"
#include "thread_pool.h"

#define TASK_QUEUE_SIZE 16
#define QUEUE_INDX_MSK 0xF

#define TASK_STATE_IDLE 0
#define TASK_STATE_RUN 1

typedef struct
{
    void *task;
    task_handler_t handler;
    task_callback_t callback;
    long delay;
    uint8_t state;
} task_container_t;

struct thread_pool
{
    pthread_t *workers;
    pthread_mutex_t lock;
    pthread_cond_t wait;
    task_handler_t handler;
    task_container_t tasks[TASK_QUEUE_SIZE];
    uint8_t to_put;
    uint8_t to_take;
    uint8_t size;
};

static void *handle_task(void *arg);

static int is_queue_full_unsafe(thread_pool_t *pool);
static int get_available(thread_pool_t *pool);

thread_pool_t *thread_pool_new(uint8_t pool_size, task_handler_t handler)
{
    thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    if (!pool)
    {
        LOG_ERR(ENOMEM, "fail to allocate (%zu)\n", sizeof(thread_pool_t));
        return NULL;
    }
    memset(pool, 0, sizeof(thread_pool_t));
    pool->workers = (pthread_t *)malloc(sizeof(pthread_t) * pool_size);

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->wait, NULL);
    pool->handler = handler;
    uint8_t widx;
    for (widx = 0; widx < pool_size; widx++)
    {
        pthread_t *thread = &pool->workers[widx];
        pthread_create(thread, NULL, handle_task, pool);
    }
    return pool;
}

int thread_pool_submit(thread_pool_t *pool, void *task, task_callback_t callback, long time_delay)
{
    if (!pool || !task)
    {
        return -1;
    }
    int task_id = -1;
    if (!pthread_mutex_lock(&pool->lock))
    {
        if (!is_queue_full_unsafe(pool))
        {
            pool->to_put &= QUEUE_INDX_MSK;
            task_id = pool->to_put++;
            pool->size++;
            task_container_t *task_container = &pool->tasks[task_id];
            task_container->task = task;
            task_container->callback = callback;
            task_container->delay = time_delay;
            task_container->handler = pool->handler;
            task_container->state = TASK_STATE_IDLE;
            pthread_cond_signal(&pool->wait);
        }
        pthread_mutex_unlock(&pool->lock);
    }
    return task_id;
}

static void *handle_task(void *arg)
{
    if (!arg)
    {
        return NULL;
    }
    thread_pool_t *pool = (thread_pool_t *)arg;
    while (TRUE)
    {
        if (!pthread_mutex_lock(&pool->lock))
        {
            while (!pool->size)
            {
                LOG_DBG("thread will block until task is available\n");
                if (pthread_cond_wait(&pool->wait, &pool->lock))
                {
                    LOG_ERR(EACCES, "error on wakeup\n");
                    return NULL;
                }
            }
            LOG_DBG("start handle task\n");
            pool->to_take &= QUEUE_INDX_MSK;
            task_container_t *container = &pool->tasks[pool->to_take++];
            pool->size--;
            if (container->state != TASK_STATE_IDLE)
            {
                LOG_DBG("task is not initialized properly\n");
                if (container->callback)
                {
                    if (pthread_mutex_unlock(&pool->lock))
                    {
                        return NULL;
                    }
                    container->callback(FAIL, container->task);
                }
                continue;
            }
            container->state = TASK_STATE_RUN;
            if (pthread_mutex_unlock(&pool->lock))
            {
                return NULL;
            }
            if (container->delay > 0)
            {
                LOG_DBG("task sleep %ld (ms)\n", container->delay);
                usleep(container->delay * 1000);
            }
            task_result_t res = container->handler(container->task);
            LOG_DBG("task result : %d\n", res);
            container->callback(res, container->task);
            container->state = TASK_STATE_IDLE;
        }
        else
        {
            return NULL;
        }
    }
}

static int is_queue_full_unsafe(thread_pool_t *pool)
{
    if (!pool)
    {
        return TRUE;
    }
    return !(get_available(pool) > 0);
}

static int get_available(thread_pool_t *pool)
{
    int gap = pool->to_put - pool->to_take;
    if (gap <= 0)
    {
        gap += TASK_QUEUE_SIZE;
    }
    return gap - 1;
}
