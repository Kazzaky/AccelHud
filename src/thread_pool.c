#include "thread_pool.h"

void thread_pool_init(thread_pool_t* pool, int thread_count)
{
  pool->thread_count = thread_count;
  pool->stop = 0;
  pool->work_available = 0;
  pool->threads_done = 0;

  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->work_cond, NULL);
  pthread_cond_init(&pool->done_cond, NULL);

  for (int i = 0; i < thread_count; i++) {
    pthread_create(&pool->threads[i], NULL, worker_thread, pool);
  }
}

void thread_pool_destroy(thread_pool_t* pool)
{
  pthread_mutex_lock(&pool->lock);
  pool->stop = 1;
  pthread_cond_broadcast(&pool->work_cond);
  pthread_mutex_unlock(&pool->lock);

  for (int i = 0; i < pool->thread_count; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  pthread_mutex_destroy(&pool->lock);
  pthread_cond_destroy(&pool->work_cond);
  pthread_cond_destroy(&pool->done_cond);
}

void* worker_thread(void* arg)
{
  thread_pool_t *pool = (thread_pool_t*)arg;

  int thread_id = -1;
  pthread_mutex_lock(&pool->lock);
  for (int i = 0; i < pool->thread_count; i++) {
    if (pthread_equal(pool->threads[i], pthread_self())) {
      thread_id = i;
      break;
    }
  }
  pthread_mutex_unlock(&pool->lock);

  while(1)
  {
    pthread_mutex_lock(&pool->lock);
    while (!pool->work_available && !pool->stop) {
      pthread_cond_wait(&pool->work_cond, &pool->lock);
    }

    if (pool->stop) {
      pthread_mutex_unlock(&pool->lock);
      break;
    }

    void *user_data = pool->user_data;
    pthread_mutex_unlock(&pool->lock);

    pool->work_func(thread_id, pool->thread_count, user_data);

    pthread_mutex_lock(&pool->lock);
    pool->threads_done++;
    if (pool->threads_done == pool->thread_count) {
      pool->work_available = 0;
      pthread_cond_signal(&pool->done_cond);
    }
    pthread_mutex_unlock(&pool->lock);
  }

    return NULL;
}

void thread_pool_run(thread_pool_t* pool, void (*func)(int, int, void*), void* user_data)
{
  pthread_mutex_lock(&pool->lock);
  pool->work_func = func;
  pool->user_data = user_data;
  pool->threads_done = 0;
  pool->work_available = 1;

  pthread_cond_broadcast(&pool->work_cond);
  
  while (pool->threads_done < pool->thread_count) {
    pthread_cond_wait(&pool->done_cond, &pool->lock);
  }

  pthread_mutex_unlock(&pool->lock);
}
