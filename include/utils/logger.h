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
 */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * Signals the logger thread to stop processing new messages (after draining).
 * This must be called BEFORE joining the logger thread.
 */
void logger_request_stop(void);

/**
 * Destroys mutexes and condition variables.
 * This must be called AFTER joining the logger thread.
 */
void logger_destroy(void);

#endif // LOGGER_H
