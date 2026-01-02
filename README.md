# Real-Time Dynamic Periodic Task Supervisor

**Course:** Concurrent and Real-Time Programming (CRTP)  
**University of Padova** **Professors:** Andrea Rigoni Garola, Gabriele Manduchi

---

## 🚀 Project Overview

This project implements a soft/hard real-time supervisor capable of dynamically accepting task requests over TCP, verifying system schedulability using **Response Time Analysis (RTA)**, and executing accepted tasks with strict timing guarantees using `SCHED_FIFO`.

### Features
* **Zero-Accumulated Drift:** Uses `clock_nanosleep` with `TIMER_ABSTIME`.
* **I/O Multiplexing:** Single-threaded network core using `poll()`.
* **Concurrency:** Thread-safe supervisor queue and atomic shutdowns.
* **Memory Safety:** Robust handling of TCP fragmentation and buffer overflows.

---

## 🛠 Build & Run

### Prerequisites
* Linux (Required for `SCHED_FIFO` and `pthread_setaffinity_np`)
* CMake >= 3.16
* GCC/Clang
* Python 3 (for testing)

### Compilation
```bash
bash build.sh
```

### Running
**Note:** Root privileges are required to set Real-Time priorities (`SCHED_FIFO`).

```bash
sudo ./build/dynamic_periodic_task
```

### Running Tests
The test suite handles startup timing automatically, even under Valgrind.

```bash
sudo bash build.sh --test
```

---

## 📡 Protocol

Connect via Telnet/Netcat on port `8080`:

* `ACTIVATE <task_name>`: Starts a task (e.g., `A t1`). Returns `ID=<id>`.
* `DEACTIVATE <id>`: Stops a specific instance (e.g., `D 1`).
* `LIST`: Shows active instances.
* `INFO`: Shows task catalog and system capacity.
* `SHUTDOWN`: Gracefully terminates the server.
