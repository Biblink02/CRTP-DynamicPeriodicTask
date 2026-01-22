#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <signal.h>
#include "supervisor.h"
#include "net_core.h"
#include "constants.h"
#include "task_runtime.h"
#include "task_routines.h"
#include "logger.h"

#define LOGGER_DESTROY  do {                                \
                            logger_request_stop();          \
                            pthread_join(log_thread, NULL); \
                            logger_destroy();}              \
                        while(false)

atomic_bool keep_running = ATOMIC_VAR_INIT(true);

static void sigusr1_handler(const int signum) { (void) signum; }

static void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
}

static void *network_entry(void *arg) {
    (void) arg;
    while (atomic_load(&keep_running)) net_poll();
    return NULL;
}

static void *supervisor_entry(void *arg) {
    (void) arg;
    supervisor_loop();
    atomic_store(&keep_running, false);
    return NULL;
}

static void set_fifo_priority(pthread_attr_t *attr, const int prio) {
    const struct sched_param param = {.sched_priority = prio};
    pthread_attr_init(attr);
    pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(attr, SCHED_FIFO);
    pthread_attr_setschedparam(attr, &param);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setup_signals();

    if (geteuid() != 0) {
        fprintf(stderr, "WARNING: Not running as root. SCHED_FIFO tasks may fail.\n");
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_NUMBER, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("[Main] Failed to set CPU affinity");
    }

    // Init Subsystems
    if (logger_init() != 0) return EXIT_FAILURE;
    if (supervisor_init() != 0) return EXIT_FAILURE;
    if (routines_init() != 0) return EXIT_FAILURE;
    if (runtime_init() != 0) return EXIT_FAILURE;

    if (net_init(SERVER_PORT) < 0) return EXIT_FAILURE;

    pthread_t net_thread, sv_thread, log_thread;
    pthread_attr_t net_attr, sv_attr, log_attr;

    /* Priorities Configuration
       Network:    99 (Highest - I/O Hardware)
       Supervisor: 98 (Logic Core)
       Tasks:      90-2 (Application)
       Logger:     1  (Lowest - Deferred I/O)
    */
    set_fifo_priority(&net_attr, 99);
    set_fifo_priority(&sv_attr, 98);
    set_fifo_priority(&log_attr, 1);

    if (pthread_create(&log_thread, &log_attr, logger_thread_entry, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to create Logger thread\n");
        logger_destroy(); // Thread never started, just clean mutexes
        return EXIT_FAILURE;
    }

    if (pthread_create(&sv_thread, &sv_attr, supervisor_entry, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to create Supervisor thread\n");
        // Shutdown Logger
        LOGGER_DESTROY;
        return EXIT_FAILURE;
    }

    if (pthread_create(&net_thread, &net_attr, network_entry, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to create Network thread\n");
        atomic_store(&keep_running, false);
        pthread_join(sv_thread, NULL);
        LOGGER_DESTROY;
        return EXIT_FAILURE;
    }

    pthread_join(sv_thread, NULL);

    pthread_join(net_thread, NULL);

    LOGGER_DESTROY;

    net_cleanup();
    runtime_cleanup();

    return EXIT_SUCCESS;
}
