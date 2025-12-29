#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include "supervisor.h"
#include "net_core.h"
#include "constants.h"

// Set CPU affinity to Core 0 to ensure deterministic execution for RTA
static int set_single_core_affinity(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);

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
}

int main(void) {
    printf("[Main] =========================================\n");
    printf("[Main] Starting Real-Time Task Supervisor System\n");
    printf("[Main] =========================================\n");

    // Set CPU Affinity
    if (set_single_core_affinity() < 0) {
        fprintf(stderr, "[Main] CRITICAL: Failed to set CPU affinity. RTA will be invalid.\n");
        return EXIT_FAILURE;
    }
    printf("[Main] CPU Affinity set to Core 0.\n");

    // Initialize Network Layer
    if (net_init(SERVER_PORT) < 0) {
        fprintf(stderr, "[Main] CRITICAL: Error initializing network on port %d\n", SERVER_PORT);
        return EXIT_FAILURE;
    }

    // Spawn Network Thread
    pthread_t net_thread;
    if (pthread_create(&net_thread, NULL, network_thread_func, NULL) != 0) {
        fprintf(stderr, "[Main] CRITICAL: Error creating network thread\n");
        return EXIT_FAILURE;
    }
    printf("[Main] Network thread created successfully.\n");

    // Enter Supervisor Loop
    printf("[Main] Handing over control to Supervisor Loop...\n");
    supervisor_loop();

    pthread_join(net_thread, NULL);

    return EXIT_SUCCESS;
}
