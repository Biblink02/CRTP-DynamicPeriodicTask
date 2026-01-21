# Real-Time Dynamic Periodic Task Supervisor

**University of Padova**


**Course:** Concurrent and Real-Time Programming (CRTP)

**Professors:** Andrea Rigoni Garola, Gabriele Manduchi

## Project Overview
This project implements a real-time supervisor that accepts task activation/deactivation requests over TCP and admits tasks only if the system remains schedulable via **Response Time Analysis (RTA)**. Accepted tasks run as `SCHED_FIFO` threads, with fixed priorities mapped from their periods (shorter period ⇒ higher priority, RMS-style).

### Key Features
- **Admission control (RTA):** Before spawning a new instance, the supervisor runs a utilization check and an RTA test against each task deadline.
- **Zero accumulated drift:** Each task sleeps using `clock_nanosleep(..., TIMER_ABSTIME, ...)` to keep a stable time grid.
- **I/O multiplexing:** A single network thread uses `poll()` with non-blocking sockets (`O_NONBLOCK`) to serve multiple clients.
- **Memory safety:** Per-client buffering handles TCP fragmentation and detects buffer overflows.
- **Non-blocking logging:** Real-time threads avoid blocking on `printf()` by logging through a low-priority logger thread using a `try_lock` approach on the buffer.

## Assignment
The goal is to define a fixed catalog of periodic routines. A TCP server must accept activation/deactivation commands; each activation spawns a new thread instance. Before accepting a new activation, Response Time Analysis determines if the system stays schedulable.

## Task Catalog
The project includes a predefined catalog defined in `src/task_routines.c`:

| Task | WCET (ms) | Period (ms) | Deadline (ms) |
| :--- | :-------: | :---------: | :-----------: |
| **t1** | 50        | 300         | 300           |
| **t2** | 100       | 500         | 500           |
| **t3** | 200       | 1000        | 1000          |

## Build & Run

### Prerequisites
- Linux (required for `SCHED_FIFO` and CPU affinity).


- CMake ≥ 3.16, GCC/Clang, Ninja, Python 3 (for tests).
- Else there is a Makefile to compile with `make`

### Compilation
```bash
bash build.sh
```

### Running
Root privileges are typically required to run with real-time scheduling.
```bash
sudo ./build/dynamic_periodic_task
```

### Running Tests
```bash
sudo bash build.sh --test
```

## Protocol
Connect via Telnet/Netcat on port **8080**.

- `ACTIVATE <task_name>` (or `A`): Start a new instance. Returns `ID=<id>`.
- `DEACTIVATE <id>` (or `D`): Stop a specific instance.
- `LIST` (or `L`): List active instances and current CPU load estimate.
- `INFO` (or `I`): Show task catalog and system capacity.
- `SHUTDOWN` (or `S`): Terminate the server.
