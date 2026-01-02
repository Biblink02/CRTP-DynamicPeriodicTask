#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <errno.h>
#include <signal.h>
#include "supervisor.h"
#include "net_core.h"
#include "constants.h"
#include "task_runtime.h"
#include "task_routines.h"

atomic_bool keep_running = ATOMIC_VAR_INIT(true);

// Empty handler to interrupt blocking syscalls (e.g., nanosleep)
static void sigusr1_handler(int signum) { (void)signum; }

static void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = 0; // No SA_RESTART: ensure blocking calls return EINTR
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
}

static void *network_entry(void *arg) {
    while (atomic_load(&keep_running)) net_poll();
    return NULL;
}

static void *supervisor_entry(void *arg) {
    supervisor_loop();
    atomic_store(&keep_running, false);
    return NULL;
}

static void set_fifo_priority(pthread_attr_t *attr, int prio) {
    struct sched_param param = { .sched_priority = prio };
    pthread_attr_init(attr);
    pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(attr, SCHED_FIFO);
    pthread_attr_setschedparam(attr, &param);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for real-time logs
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

    /* Initialize ALL internal subsystems before creating threads or opening sockets.
       This prevents race conditions where the network thread accepts a client
       before the supervisor or task lists are ready. */

    supervisor_init();
    routines_init();   // Blocking CPU calibration
    runtime_init();

    // Open network port only after internals are ready
    if (net_init(SERVER_PORT) < 0) return EXIT_FAILURE;

    pthread_t net_thread, sv_thread;
    pthread_attr_t net_attr, sv_attr;

    // Priorities: Network (99) > Supervisor (98) > Tasks (max 90)
    set_fifo_priority(&net_attr, 99);
    set_fifo_priority(&sv_attr, 98);

    if (pthread_create(&sv_thread, &sv_attr, supervisor_entry, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to create Supervisor thread\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&net_thread, &net_attr, network_entry, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to create Network thread\n");
        atomic_store(&keep_running, false);
        pthread_join(sv_thread, NULL);
        return EXIT_FAILURE;
    }

    pthread_join(sv_thread, NULL);
    pthread_join(net_thread, NULL);

    net_cleanup();
    runtime_cleanup();

    return EXIT_SUCCESS;
}