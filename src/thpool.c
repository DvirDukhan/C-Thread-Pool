/* ********************************
 * Author:       Johan Hanssen Seferidis
 * License:	     MIT
 * Description:  Library providing a threading pool where you can add
 *               work. For usage, check the thpool.h file or README.md
 *
 */
/** @file thpool.h */ /*
                       *
                       ********************************/

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "thpool.h"

#ifdef THPOOL_DEBUG
#define THPOOL_DEBUG 1
#else
#define THPOOL_DEBUG 0
#endif

#if !defined(DISABLE_PRINT) || defined(THPOOL_DEBUG)
#define err(str) fprintf(stderr, str)
#else
#define err(str)
#endif

static volatile int threads_keepalive;
static volatile int threads_on_hold;

/* ========================== STRUCTURES ============================ */

/* Job */
typedef struct job {
    struct job *prev;            /* pointer to previous job   */
    void (*function)(void *arg); /* function pointer          */
    void *arg;                   /* function's argument       */
} job;

/* Job queue */
typedef struct jobqueue {
    pthread_mutex_t rwmutex; /* used for queue r/w access */
    job *front;              /* pointer to front of queue */
    job *rear;               /* pointer to rear  of queue */
    pthread_cond_t has_jobs; /* signal on job enqueuing   */
    int len;                 /* number of jobs in queue   */
} jobqueue;

/* Thread */
typedef struct thread {
    int id;                   /* friendly id               */
    pthread_t pthread;        /* pointer to actual thread  */
    struct thpool_ *thpool_p; /* access to thpool          */
} thread;

/* Threadpool */
typedef struct thpool_ {
    thread **threads;                /* pointer to threads        */
    atomic_uint num_threads_alive;   /* threads currently alive   */
    atomic_uint num_threads_working; /* threads currently working */
    atomic_uint tasks;               /* number of tasks           */
    jobqueue jobqueue;               /* job queue                 */

} thpool_;

/* ========================== PROTOTYPES ============================ */

static int thread_init(thpool_ *thpool_p, struct thread **thread_p, int id);
static void *thread_do(struct thread *thread_p);
static void thread_hold(int sig_id);
static void thread_destroy(struct thread *thread_p);

static int jobqueue_init(jobqueue *jobqueue_p);
static void jobqueue_clear(jobqueue *jobqueue_p);
static void jobqueue_push(jobqueue *jobqueue_p, struct job *newjob_p);
static struct job *jobqueue_pull(jobqueue *jobqueue_p);
static void jobqueue_destroy(jobqueue *jobqueue_p);

/* ========================== THREADPOOL ============================ */

/* Initialise thread pool */
struct thpool_ *thpool_init(int num_threads) {

    threads_on_hold = 0;
    threads_keepalive = 1;

    if (num_threads < 0) {
        num_threads = 0;
    }

    /* Make new thread pool */
    thpool_ *thpool_p;
    thpool_p = (struct thpool_ *)malloc(sizeof(struct thpool_));
    if (thpool_p == NULL) {
        err("thpool_init(): Could not allocate memory for thread pool\n");
        return NULL;
    }
    atomic_init(&thpool_p->num_threads_alive, 0);
    atomic_init(&thpool_p->num_threads_working, 0);
    atomic_init(&thpool_p->tasks, 0);

    /* Initialise the job queue */
    if (jobqueue_init(&thpool_p->jobqueue) == -1) {
        err("thpool_init(): Could not allocate memory for job queue\n");
        free(thpool_p);
        return NULL;
    }

    /* Make threads in pool */
    thpool_p->threads = (struct thread **)malloc(num_threads * sizeof(struct thread *));
    if (thpool_p->threads == NULL) {
        err("thpool_init(): Could not allocate memory for threads\n");
        jobqueue_destroy(&thpool_p->jobqueue);
        free(thpool_p);
        return NULL;
    }

    /* Thread init */
    int n;
    for (n = 0; n < num_threads; n++) {
        thread_init(thpool_p, &thpool_p->threads[n], n);
#if THPOOL_DEBUG
        printf("THPOOL_DEBUG: Created thread %d in pool \n", n);
#endif
    }

    /* Wait for threads to initialize */
    while (atomic_load(&thpool_p->num_threads_alive) != num_threads) {
    }

    return thpool_p;
}

/* Add work to the thread pool */
int thpool_add_work(thpool_ *thpool_p, void (*function_p)(void *), void *arg_p) {
    job *newjob;

    newjob = (struct job *)malloc(sizeof(struct job));
    if (newjob == NULL) {
        err("thpool_add_work(): Could not allocate memory for new job\n");
        return -1;
    }

    /* add function and argument */
    newjob->function = function_p;
    newjob->arg = arg_p;

    /* add job to queue */
    jobqueue_push(&thpool_p->jobqueue, newjob);
    atomic_fetch_add(&thpool_p->tasks, 1);

    return 0;
}

/* Wait until all jobs have finished */
void thpool_wait(thpool_ *thpool_p) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 500000L;
    while (atomic_load(&thpool_p->tasks) > 0) {
        // sleep for 0.5 miliseconds
        nanosleep(&ts, NULL);
    }
}

/* Destroy the threadpool */
void thpool_destroy(thpool_ *thpool_p) {
    /* No need to destroy if it's NULL */
    if (thpool_p == NULL)
        return;

    volatile int threads_total = thpool_p->num_threads_alive;

    /* End each thread 's infinite loop */
    threads_keepalive = 0;

    /* Give one second to kill idle threads */
    double TIMEOUT = 1.0;
    time_t start, end;
    double tpassed = 0.0;
    time(&start);
    while (tpassed < TIMEOUT && atomic_load(&thpool_p->num_threads_alive)) {
        pthread_mutex_lock(&thpool_p->jobqueue.rwmutex);
        pthread_cond_signal(&thpool_p->jobqueue.has_jobs);
        pthread_mutex_unlock(&thpool_p->jobqueue.rwmutex);
        time(&end);
        tpassed = difftime(end, start);
    }

    /* Poll remaining threads */
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10000000L; /* 10ms */
    while (atomic_load(&thpool_p->num_threads_alive)) {
        pthread_mutex_lock(&thpool_p->jobqueue.rwmutex);
        pthread_cond_signal(&thpool_p->jobqueue.has_jobs);
        pthread_mutex_unlock(&thpool_p->jobqueue.rwmutex);
        nanosleep(&ts, NULL);
    }

    /* Job queue cleanup */
    jobqueue_destroy(&thpool_p->jobqueue);
    /* Deallocs */
    int n;
    for (n = 0; n < threads_total; n++) {
        thread_destroy(thpool_p->threads[n]);
    }
    free(thpool_p->threads);
    free(thpool_p);
}

/* Pause all threads in threadpool */
void thpool_pause(thpool_ *thpool_p) {
    int n;
    for (n = 0; n < thpool_p->num_threads_alive; n++) {
        pthread_kill(thpool_p->threads[n]->pthread, SIGUSR1);
    }
}

/* Resume all threads in threadpool */
void thpool_resume(thpool_ *thpool_p) {
    // resuming a single threadpool hasn't been
    // implemented yet, meanwhile this suppresses
    // the warnings
    (void)thpool_p;

    threads_on_hold = 0;
}

int thpool_num_threads_working(thpool_ *thpool_p) {
    return atomic_load(&thpool_p->num_threads_working);
}

/* ============================ THREAD ============================== */

/* Initialize a thread in the thread pool
 *
 * @param thread        address to the pointer of the thread to be created
 * @param id            id to be given to the thread
 * @return 0 on success, -1 otherwise.
 */
static int thread_init(thpool_ *thpool_p, struct thread **thread_p, int id) {

    *thread_p = (struct thread *)malloc(sizeof(struct thread));
    if (*thread_p == NULL) {
        err("thread_init(): Could not allocate memory for thread\n");
        return -1;
    }

    (*thread_p)->thpool_p = thpool_p;
    (*thread_p)->id = id;

    pthread_create(&(*thread_p)->pthread, NULL, (void *(*)(void *))thread_do, (*thread_p));
    pthread_detach((*thread_p)->pthread);
    return 0;
}

/* Sets the calling thread on hold */
static void thread_hold(int sig_id) {
    (void)sig_id;
    threads_on_hold = 1;
    while (threads_on_hold) {
        sleep(1);
    }
}

/* What each thread is doing
 *
 * In principle this is an endless loop. The only time this loop gets
 * interuppted is once thpool_destroy() is invoked or the program exits.
 *
 * @param  thread        thread that will run this function
 * @return nothing
 */
static void *thread_do(struct thread *thread_p) {

    /* Set thread name for profiling and debugging */
    char thread_name[16] = {0};
    snprintf(thread_name, 16, "thpool-%d", thread_p->id);

#if defined(__linux__)
    /* Use prctl instead to prevent using _GNU_SOURCE flag and implicit
     * declaration */
    prctl(PR_SET_NAME, thread_name);
#elif defined(__APPLE__) && defined(__MACH__)
    pthread_setname_np(thread_name);
#else
    err("thread_do(): pthread_setname_np is not supported on this system");
#endif

    /* Assure all threads have been created before starting serving */
    thpool_ *thpool_p = thread_p->thpool_p;

    /* Register signal handler */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = thread_hold;
    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        err("thread_do(): cannot handle SIGUSR1");
    }

    /* Mark thread as alive (initialized) */
    atomic_fetch_add(&thpool_p->num_threads_alive, 1);

    while (threads_keepalive) {

        job *job_p = jobqueue_pull(&thpool_p->jobqueue);
        atomic_fetch_add(&thpool_p->num_threads_working, 1);
        if (job_p) {
            void (*func_buff)(void *) = job_p->function;
            void *arg_buff = job_p->arg;
            func_buff(arg_buff);
            free(job_p);
            atomic_fetch_sub(&thpool_p->tasks, 1);
        }
        atomic_fetch_sub(&thpool_p->num_threads_working, 1);
    }
    atomic_fetch_sub(&thpool_p->num_threads_alive, 1);

    return NULL;
}

/* Frees a thread  */
static void thread_destroy(thread *thread_p) { free(thread_p); }

/* ============================ JOB QUEUE =========================== */

/* Initialize queue */
static int jobqueue_init(jobqueue *jobqueue_p) {
    jobqueue_p->len = 0;
    jobqueue_p->front = NULL;
    jobqueue_p->rear = NULL;

    pthread_mutex_init(&(jobqueue_p->rwmutex), NULL);
    pthread_cond_init(&(jobqueue_p->has_jobs), NULL);

    return 0;
}

/* Clear the queue */
static void jobqueue_clear(jobqueue *jobqueue_p) {

    while (jobqueue_p->len) {
        free(jobqueue_pull(jobqueue_p));
    }

    jobqueue_p->front = NULL;
    jobqueue_p->rear = NULL;
    jobqueue_p->len = 0;
}

/* Add (allocated) job to queue
 */
static void jobqueue_push(jobqueue *jobqueue_p, struct job *newjob) {

    pthread_mutex_lock(&jobqueue_p->rwmutex);
    newjob->prev = NULL;

    switch (jobqueue_p->len) {

    case 0: /* if no jobs in queue */
        jobqueue_p->front = newjob;
        jobqueue_p->rear = newjob;
        break;

    default: /* if jobs in queue */
        jobqueue_p->rear->prev = newjob;
        jobqueue_p->rear = newjob;
    }
    jobqueue_p->len++;
    pthread_cond_signal(&jobqueue_p->has_jobs);
    pthread_mutex_unlock(&jobqueue_p->rwmutex);
}

/* Get first job from queue(removes it from queue)
 * Notice: Caller MUST hold a mutex
 */
static struct job *jobqueue_pull(jobqueue *jobqueue_p) {

    pthread_mutex_lock(&jobqueue_p->rwmutex);
    pthread_cond_wait(&jobqueue_p->has_jobs, &jobqueue_p->rwmutex);
    // the above line will block until a job is added to the queue
    // and the thread is woken up with the mutex locked
    job *job_p = jobqueue_p->front;

    switch (jobqueue_p->len) {

    case 0: /* if no jobs in queue */
        break;

    case 1: /* if one job in queue */
        jobqueue_p->front = NULL;
        jobqueue_p->rear = NULL;
        jobqueue_p->len = 0;
        break;

    default: /* if >1 jobs in queue */
        jobqueue_p->front = job_p->prev;
        jobqueue_p->len--;
        /* more than one job in queue -> post it */
        pthread_cond_signal(&jobqueue_p->has_jobs);
    }

    pthread_mutex_unlock(&jobqueue_p->rwmutex);
    return job_p;
}

/* Free all queue resources back to the system */
static void jobqueue_destroy(jobqueue *jobqueue_p) { jobqueue_clear(jobqueue_p); }
