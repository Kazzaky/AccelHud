#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

#define MAX_THREADS 16

typedef struct {
  pthread_t       threads[MAX_THREADS];
  int             thread_count;

  pthread_mutex_t lock; // this struct lock
  pthread_cond_t  work_cond;
  pthread_cond_t  done_cond;

  int             work_available;
  int             stop;
  int             threads_done;

  void            *user_data;
  void (*work_func)(int thread_id, int total_threads, void* user_data);
} thread_pool_t;

void thread_pool_init(thread_pool_t* pool, int thread_count);
void thread_pool_destroy(thread_pool_t* pool);
void* worker_thread(void* arg);
void thread_pool_run(thread_pool_t* pool, void (*func)(int, int, void*), void* user_data);

#endif // THREAD_POOL_H
