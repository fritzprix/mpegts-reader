#ifndef __THREAD_POOL_H
#define __THREAD_POOL_H

#include <stdint.h>

#define RESULT_FAIL
#define RESULT_OK

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OK,
    FAIL
} task_result_t;

typedef struct thread_pool thread_pool_t;
typedef task_result_t (*task_handler_t)(void* task);
typedef void (*task_callback_t)(task_result_t result, void* task);


extern thread_pool_t* thread_pool_new(uint8_t pool_size, task_handler_t handler);
extern int thread_pool_submit( thread_pool_t* pool, void* task, task_callback_t callback, long time_delay);


#ifdef __cplusplus
}
#endif

#endif