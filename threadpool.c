#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "threadpool.h"

// Create a thread pool with the specified number of threads
threadpool* create_threadpool(int num_threads_in_pool) {
    if (num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL) {
        perror("Error creating thread pool: Invalid number of threads");
        exit(EXIT_FAILURE);
    }

    // Allocate memory for the threadpool structure
    threadpool* pool = (threadpool*)malloc(sizeof(threadpool));
    if (pool == NULL) {
        perror("Error allocating memory for thread pool");
        exit(EXIT_FAILURE);
    }

    // Initialize the thread pool structure
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->threads = (pthread_t*)malloc(num_threads_in_pool * sizeof(pthread_t));
    if (pool->threads == NULL) {
        perror("Error allocating memory for threads array");
        free(pool);
        exit(EXIT_FAILURE);
    }

    pool->qhead = pool->qtail = NULL;
    pool->shutdown = pool->dont_accept = 0;

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&(pool->qlock), NULL) != 0) {
        perror("Error initializing mutex");
        free(pool->threads);
        free(pool);
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&(pool->q_not_empty), NULL) != 0 || pthread_cond_init(&(pool->q_empty), NULL) != 0) {
        perror("Error initializing condition variables");
        pthread_mutex_destroy(&(pool->qlock));
        free(pool->threads);
        free(pool);
        exit(EXIT_FAILURE);
    }

    // Create the threads
    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, do_work, (void*)pool) != 0) {
            perror("Error creating thread");
            destroy_threadpool(pool);
            exit(EXIT_FAILURE);
        }
    }

    return pool;
}

// Worker function executed by each thread in the thread pool
void* do_work(void* p) {
    threadpool* pool = (threadpool*)p;

    while (1) {
        pthread_mutex_lock(&(pool->qlock));

        if (pool->shutdown) {
            pthread_mutex_unlock(&(pool->qlock));
            pthread_exit(NULL);
        }

        while (pool->qsize == 0 && !pool->shutdown) {
            pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock));
        }

        if (pool->qhead != NULL) {
            work_t* task = pool->qhead;
            pool->qhead = task->next;
            pool->qsize--;

            if (pool->qsize == 0 && pool->dont_accept) {
                pthread_cond_signal(&(pool->q_empty));
            }

            pthread_mutex_unlock(&(pool->qlock));

            task->routine(task->arg);

            free(task);
        } else {
            pthread_mutex_unlock(&(pool->qlock));
        }
    }
}

// Add a task to the thread pool's queue
void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg) {
    pthread_mutex_lock(&(from_me->qlock));

    work_t* new_work = (work_t*)malloc(sizeof(work_t));
    if (new_work == NULL) {
        perror("Error allocating memory for work_t");
        pthread_mutex_unlock(&(from_me->qlock));
        return;
    }

    new_work->routine = dispatch_to_here;
    new_work->arg = arg;
    new_work->next = NULL;

    if (from_me->dont_accept == 1) {
        fprintf(stderr, "Destruction process has begun. Cannot accept new items to the queue.\n");
        free(new_work);
        pthread_mutex_unlock(&(from_me->qlock));
        return;
    }

    if (from_me->qsize == 0) {
        from_me->qhead = from_me->qtail = new_work;
    } else {
        from_me->qtail->next = new_work;
        from_me->qtail = new_work;
    }

    from_me->qsize++;

    pthread_cond_signal(&(from_me->q_not_empty));

    pthread_mutex_unlock(&(from_me->qlock));
}

// Destroy the given thread pool and free associated resources
void destroy_threadpool(threadpool* destroyme) {
    if (destroyme == NULL) {
        fprintf(stderr, "Error: Attempting to destroy NULL threadpool\n");
        return;
    }

    pthread_mutex_lock(&(destroyme->qlock));

    destroyme->dont_accept = 1;

    while (destroyme->qsize > 0) {
        pthread_cond_wait(&(destroyme->q_empty), &(destroyme->qlock));
    }

    destroyme->shutdown = 1;

    pthread_cond_broadcast(&(destroyme->q_not_empty));

    pthread_mutex_unlock(&(destroyme->qlock));

    for (int i = 0; i < destroyme->num_threads; i++) {
        if (pthread_join(destroyme->threads[i], NULL) != 0) {
            perror("Error joining thread");
        }
    }

    free(destroyme->threads);
    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));
    free(destroyme);
}
