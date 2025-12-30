#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include "supervisor.h"
#include "net_core.h"
#include "constants.h"
#include "task_runtime.h"

static int set_single_core_affinity(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(CPU_NUMBER, &set);

    if (sched_setaffinity(0, sizeof(set), &set) == -1) {
        perror("[Main] sched_setaffinity failed");
        return -1;
    }
    return 0;
}

void *network_thread_func(void *arg) {
    printf("[Network] Thread started. Polling on port %d...\n", SERVER_PORT);

    while (1) {
        net_poll();
        usleep(1000);
    }
    return NULL;
}

void *supervisor_thread_func(void *arg) {
    // Blocks here until the system shuts down via event
    supervisor_loop();
    return NULL;
}

int main(void) {
    printf("[Main] =========================================\n");
    printf("[Main] Starting Real-Time Task Supervisor System\n");
    printf("[Main] =========================================\n");

    if (set_single_core_affinity() < 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to set CPU affinity. RTA will be invalid.\n");
        return EXIT_FAILURE;
    }
    printf("[Main] CPU Affinity set to Core 0.\n");

    if (net_init(SERVER_PORT) < 0) {
        fprintf(stderr, "[Main] CRITICAL: Error initializing network on port %d\n", SERVER_PORT);
        return EXIT_FAILURE;
    }

    pthread_t net_thread;
    if (pthread_create(&net_thread, NULL, network_thread_func, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Error creating network thread\n");
        return EXIT_FAILURE;
    }
    printf("[Main] Network thread created successfully.\n");

    pthread_t sv_thread;
    if (pthread_create(&sv_thread, NULL, supervisor_thread_func, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Error creating supervisor thread\n");
        // Clean up network before exiting
        pthread_cancel(net_thread);
        pthread_join(net_thread, NULL);
        net_cleanup();
        return EXIT_FAILURE;
    }
    printf("[Main] Supervisor thread created successfully.\n");

    // Wait for the supervisor to complete (triggered by Shutdown event)
    pthread_join(sv_thread, NULL);
    printf("[Main] Supervisor thread has exited.\n");

    // Graceful shutdown sequence
    printf("[Main] Stopping network thread...\n");
    pthread_cancel(net_thread);
    pthread_join(net_thread, NULL);

    net_cleanup();

    printf("[Main] Stopping active tasks...\n");
    runtime_cleanup();

    printf("[Main] System Shutdown Complete.\n");
    return EXIT_SUCCESS;
}