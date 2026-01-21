#ifndef LOGGER_H
#define LOGGER_H

/**
 * Initializes the mutexes, condition variables and buffers.
 * Does NOT spawn the thread.
 * @return 0 on success, -1 on failure.
 */
int logger_init(void);

/**
 * The main loop for the logger thread.
 * Consumes messages from the ring buffer and writes to stdout.
 * Intended to be run by a thread created in main.
 */
void *logger_thread_entry(void *arg);

/**
 * Non-blocking, thread-safe logging function for Real-Time tasks.
 *
 * It formats the string and copies it into a shared ring buffer.
 * If the buffer is full, the message is dropped to prevent priority inversion
 * or blocking the calling real-time task.
 *
 * @param fmt Format string (standard printf syntax).
 * @param ... Variable arguments.
 */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * Signals the logger thread to stop and releases resources.
 * Ensures remaining messages in the buffer are flushed before shutdown.
 */
void logger_cleanup(void);

#endif // LOGGER_H
