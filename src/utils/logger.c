#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "logger.h"
#include <stdbool.h>
#include "constants.h"

typedef struct {
    char buffer[LOGGER_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    atomic_bool keep_running;
    atomic_size_t dropped_logs;
} LoggerContext;

static LoggerContext ctx;

int logger_init(void) {
    memset(&ctx, 0, sizeof(LoggerContext));
    atomic_store(&ctx.keep_running, true);
    atomic_store(&ctx.dropped_logs, 0);

    if (pthread_mutex_init(&ctx.mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&ctx.cond, NULL) != 0) return -1;
    printf("[Logger] Subsystem Initialized (Buffer: %d bytes).\n", LOGGER_BUFFER_SIZE);
    return 0;
}

void *logger_thread_entry(void *arg) {
    (void)arg;
    char local_buf[MAX_LOG_MSG_LEN];

    while (atomic_load(&ctx.keep_running) || ctx.count > 0) {
        pthread_mutex_lock(&ctx.mutex);

        while (ctx.count == 0 && atomic_load(&ctx.keep_running)) {
            pthread_cond_wait(&ctx.cond, &ctx.mutex);
        }

        if (ctx.count == 0 && !atomic_load(&ctx.keep_running)) {
            pthread_mutex_unlock(&ctx.mutex);
            break;
        }

        size_t idx = 0;
        while (idx < MAX_LOG_MSG_LEN - 1 && ctx.count > 0) {
            char c = ctx.buffer[ctx.tail];
            ctx.tail = (ctx.tail + 1) % LOGGER_BUFFER_SIZE;
            ctx.count--;

            local_buf[idx++] = c;
            if (c == '\0') break;
        }
        local_buf[idx] = '\0';

        pthread_mutex_unlock(&ctx.mutex);

        fputs(local_buf, stdout);
    }
    return NULL;
}

void rt_log(const char *fmt, ...) {
    if (!atomic_load(&ctx.keep_running)) return;

    char temp_buf[MAX_LOG_MSG_LEN];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(temp_buf, sizeof(temp_buf), fmt, args);
    va_end(args);

    if (len < 0) return;

    size_t write_len = (size_t)len + 1;

    pthread_mutex_lock(&ctx.mutex);

    size_t capacity = LOGGER_BUFFER_SIZE;
    size_t free_space = capacity - ctx.count;

    if (write_len > free_space) {
        atomic_fetch_add(&ctx.dropped_logs, 1);
        pthread_mutex_unlock(&ctx.mutex);
        return;
    }

    for (size_t i = 0; i < write_len; i++) {
        ctx.buffer[ctx.head] = temp_buf[i];
        ctx.head = (ctx.head + 1) % LOGGER_BUFFER_SIZE;
    }

    ctx.count += write_len;
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mutex);
}

void logger_cleanup(void) {
    atomic_store(&ctx.keep_running, false);

    pthread_mutex_lock(&ctx.mutex);
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mutex);

    // Main joins the thread, we just report stats here

    size_t dropped = atomic_load(&ctx.dropped_logs);
    if (dropped > 0) {
        fprintf(stderr, "[Logger] Warning: %zu messages dropped.\n", dropped);
    }

    pthread_mutex_destroy(&ctx.mutex);
    pthread_cond_destroy(&ctx.cond);
}