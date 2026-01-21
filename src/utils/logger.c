#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "logger.h"
#include "constants.h"

#ifndef LOGGER_USE_TRYLOCK
#define LOGGER_USE_TRYLOCK 1
#endif

typedef struct {
    char buffer[LOGGER_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    atomic_bool keep_running;
    atomic_size_t dropped_overflow;
    atomic_size_t dropped_busy;
    atomic_size_t truncated_msgs;
} LoggerContext;

static LoggerContext ctx;

static inline void ring_write_bytes(const char *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        ctx.buffer[ctx.head] = src[i];
        ctx.head = (ctx.head + 1) % LOGGER_BUFFER_SIZE;
    }
    ctx.count += n;
}

int logger_init(void) {
    memset(&ctx, 0, sizeof(LoggerContext));
    atomic_init(&ctx.keep_running, true);
    atomic_init(&ctx.dropped_overflow, 0);
    atomic_init(&ctx.dropped_busy, 0);
    atomic_init(&ctx.truncated_msgs, 0);

    if (pthread_mutex_init(&ctx.mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&ctx.cond, NULL) != 0) {
        pthread_mutex_destroy(&ctx.mutex);
        return -1;
    }

    printf("[Logger] Subsystem Initialized (Buffer: %d bytes).\n", LOGGER_BUFFER_SIZE);
    return 0;
}

void *logger_thread_entry(void *arg) {
    (void)arg;
    char local_buf[MAX_LOG_MSG_LEN];

    while (true) {
        pthread_mutex_lock(&ctx.mutex);

        while (ctx.count == 0 && atomic_load_explicit(&ctx.keep_running, memory_order_relaxed)) {
            pthread_cond_wait(&ctx.cond, &ctx.mutex);
        }

        if (ctx.count == 0 && !atomic_load_explicit(&ctx.keep_running, memory_order_relaxed)) {
            pthread_mutex_unlock(&ctx.mutex);
            break;
        }

        size_t idx = 0;
        bool truncated = false;

        while (ctx.count > 0) {
            const char c = ctx.buffer[ctx.tail];
            ctx.tail = (ctx.tail + 1) % LOGGER_BUFFER_SIZE;
            ctx.count--;

            if (idx < MAX_LOG_MSG_LEN - 1) {
                local_buf[idx++] = c;
            } else {
                truncated = true;
            }

            if (c == '\0') break;
        }

        local_buf[idx] = '\0';
        pthread_mutex_unlock(&ctx.mutex);

        if (truncated) {
            atomic_fetch_add_explicit(&ctx.truncated_msgs, 1, memory_order_relaxed);
        }

        fputs(local_buf, stdout);
    }
    return NULL;
}

void rt_log(const char *fmt, ...) {
    if (!atomic_load_explicit(&ctx.keep_running, memory_order_relaxed)) return;

#if LOGGER_USE_TRYLOCK
    if (pthread_mutex_trylock(&ctx.mutex) != 0) {
        atomic_fetch_add_explicit(&ctx.dropped_busy, 1, memory_order_relaxed);
        return;
    }
#else
    pthread_mutex_lock(&ctx.mutex);
#endif

    if (!atomic_load_explicit(&ctx.keep_running, memory_order_relaxed)) {
        pthread_mutex_unlock(&ctx.mutex);
        return;
    }

    char temp_buf[MAX_LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(temp_buf, sizeof(temp_buf), fmt, args);
    va_end(args);

    if (len < 0) {
        pthread_mutex_unlock(&ctx.mutex);
        return;
    }

    size_t write_len;
    if ((size_t)len >= sizeof(temp_buf)) {
        temp_buf[MAX_LOG_MSG_LEN - 1] = '\0';
        write_len = sizeof(temp_buf);
        atomic_fetch_add_explicit(&ctx.truncated_msgs, 1, memory_order_relaxed);
    } else {
        write_len = (size_t)len + 1;
    }

    const size_t free_space = LOGGER_BUFFER_SIZE - ctx.count;
    if (write_len > free_space) {
        atomic_fetch_add_explicit(&ctx.dropped_overflow, 1, memory_order_relaxed);
        pthread_mutex_unlock(&ctx.mutex);
        return;
    }

    const bool was_empty = (ctx.count == 0);
    ring_write_bytes(temp_buf, write_len);

    if (was_empty) pthread_cond_signal(&ctx.cond);

    pthread_mutex_unlock(&ctx.mutex);
}

void logger_request_stop(void) {
    atomic_store_explicit(&ctx.keep_running, false, memory_order_relaxed);
    pthread_mutex_lock(&ctx.mutex);
    pthread_cond_broadcast(&ctx.cond);
    pthread_mutex_unlock(&ctx.mutex);
}

void logger_destroy(void) {
    const size_t d_over = atomic_load_explicit(&ctx.dropped_overflow, memory_order_relaxed);
    const size_t d_busy = atomic_load_explicit(&ctx.dropped_busy, memory_order_relaxed);
    const size_t trunc = atomic_load_explicit(&ctx.truncated_msgs, memory_order_relaxed);

    if (d_over) fprintf(stderr, "[Logger] Dropped (overflow): %zu\n", d_over);
    if (d_busy) fprintf(stderr, "[Logger] Dropped (mutex busy): %zu\n", d_busy);
    if (trunc) fprintf(stderr, "[Logger] Truncated: %zu\n", trunc);

    pthread_mutex_destroy(&ctx.mutex);
    pthread_cond_destroy(&ctx.cond);
    printf("[Logger] Subsystem Destroyed.\n");
}