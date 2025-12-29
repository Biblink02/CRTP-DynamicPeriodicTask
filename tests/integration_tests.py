import socket
import subprocess
import time
import sys
import os
import threading
import shutil

# Configuration
SERVER_EXE = "./dynamic_periodic_task"
PORT = 8080
HOST = "127.0.0.1"
MAX_CONCURRENT_CLIENTS = 5


#  UTILS

def get_server_command():
    """Constructs the start command, using Valgrind if available."""
    if not os.path.exists(SERVER_EXE):
        print(f"Error: Executable {SERVER_EXE} not found.")
        sys.exit(1)

    valgrind_path = shutil.which("valgrind")

    if valgrind_path:
        print(f"[{os.path.basename(__file__)}] Valgrind detected. Running with memory checks.")
        return [
            valgrind_path,
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--track-origins=yes",
            "--error-exitcode=1",  # Fail test on memory errors
            SERVER_EXE
        ]
    else:
        print(f"[{os.path.basename(__file__)}] Valgrind not found. Running natively.")
        return [SERVER_EXE]


def start_server():
    """Starts the server in a subprocess and waits for readiness."""
    cmd = get_server_command()
    # Use DEVNULL to keep test output clean
    process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Valgrind takes longer to initialize than native execution
    time.sleep(1.5 if "valgrind" in cmd[0] else 0.5)

    if process.poll() is not None:
        print(f"CRITICAL: Server failed to start (Exit Code: {process.returncode})")
        sys.exit(1)

    return process


def stop_server(process, use_valgrind):
    """Terminates the server and checks the exit code."""
    if not process: return True

    process.terminate()

    try:
        # Wait for the process to terminate and write Valgrind reports
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        print("Server hung, forced kill.")

    # If using Valgrind, exit code 1 indicates memory errors
    if use_valgrind:
        if process.returncode == 1:
            print(f"VALGRIND FAILURE: Memory errors/leaks detected!")
            return False
        elif process.returncode != 0 and process.returncode != -15:
            # -15 is SIGTERM (normal stop)
            print(f"VALGRIND WARNING: Abnormal exit code {process.returncode}")

    return True


def send_command(sock, cmd):
    """Sends a command and returns the cleaned response."""
    try:
        sock.sendall(f"{cmd}\n".encode())
        data = sock.recv(1024).decode().strip()
        return data
    except (BrokenPipeError, ConnectionResetError):
        return ""


def run_test_isolated(test_func):
    """Executes a test ensuring a fresh server instance (Setup/Teardown)."""
    cmd = get_server_command()
    is_valgrind = "valgrind" in cmd[0]

    try:
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5 if is_valgrind else 0.5)

        if process.poll() is not None:
            print(f"CRITICAL: Server failed to start (Code: {process.returncode})")
            return False
    except Exception as e:
        print(f"Exception starting server: {e}")
        return False

    test_passed = False
    try:
        test_passed = test_func()
    except Exception as e:
        print(f"Exception during test: {e}")
        test_passed = False

    # Verify server integrity during test execution
    if process.poll() is not None:
        print(f"FAIL: Server crashed during test (Code: {process.returncode})")
        return False

    valgrind_passed = stop_server(process, is_valgrind)

    if test_passed and valgrind_passed:
        return True
    else:
        return False


#  WORKER FOR CONCURRENCY

def client_worker(thread_id, results, lock):
    """Function executed by each simulated thread."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)  # Increased timeout for Valgrind
        sock.connect((HOST, PORT))

        resp = send_command(sock, "ACTIVATE t1")

        assigned_id = -1
        status = "FAIL"

        if "OK" in resp:
            try:
                assigned_id = int(resp.split("ID=")[1])
                status = "OK"
            except:
                status = "PARSE_ERR"
        elif "ERR" in resp:
            status = "SERVER_REJECT"
        else:
            status = "UNKNOWN_RESP"

        if assigned_id != -1:
            time.sleep(0.1)
            send_command(sock, f"DEACTIVATE {assigned_id}")

        sock.close()

        with lock:
            results.append({
                "thread": thread_id,
                "status": status,
                "id": assigned_id,
                "raw_resp": resp
            })

    except Exception as e:
        with lock:
            results.append({"thread": thread_id, "status": "EXCEPTION", "error": str(e)})


#  TEST CASES

def test_concurrent_clients():
    print(f"\n [TEST] Concurrent Access ({MAX_CONCURRENT_CLIENTS} Clients) ")

    threads = []
    results = []
    lock = threading.Lock()

    for i in range(MAX_CONCURRENT_CLIENTS):
        t = threading.Thread(target=client_worker, args=(i, results, lock))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    success_count = 0
    ids_obtained = []

    print(f"Results from {len(results)} threads:")
    for res in results:
        if res['status'] == "OK":
            success_count += 1
            ids_obtained.append(res['id'])
        elif res['status'] == "EXCEPTION":
            print(f"   Error: {res.get('error')}")
            return False

    if len(ids_obtained) != len(set(ids_obtained)):
        print(f"FAIL: Duplicate IDs detected! {ids_obtained}")
        return False

    print(f"PASS: {success_count}/{MAX_CONCURRENT_CLIENTS} served. Unique IDs: {ids_obtained}")
    return True


def test_protocol_failure_injection():
    print("\n [TEST] Failure Injection & Protocol ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        if "ERR" not in send_command(sock, "GARBAGE"): return False
        if "ERR" not in send_command(sock, "DEACTIVATE 999"): return False
        sock.close()

        time.sleep(0.2)
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.connect((HOST, PORT))
        sock2.close()
        print("PASS: Server robust.")
        return True
    except:
        return False


def test_schedulability_saturation():
    print("\n [TEST] Schedulability Saturation ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        accepted = 0
        for i in range(10):
            if "OK" in send_command(sock, "ACTIVATE t3"):
                accepted += 1
            else:
                print(f"PASS: Saturation reached at {accepted} tasks.")
                sock.close()
                return accepted >= 1
        return False
    except:
        return False


def test_dynamic_stress():
    print("\n [TEST] Dynamic Stress ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        resp = send_command(sock, "ACTIVATE t1")
        if "OK" not in resp: return False
        tid = int(resp.split("ID=")[1])

        if "OK" not in send_command(sock, f"DEACTIVATE {tid}"): return False

        resp2 = send_command(sock, "ACTIVATE t1")
        tid2 = int(resp2.split("ID=")[1])

        print(f"PASS: Cycle OK. IDs: {tid} -> {tid2}")
        return True
    except:
        return False


def run_suite():
    tests = [
        test_protocol_failure_injection,
        test_schedulability_saturation,
        test_dynamic_stress,
        test_concurrent_clients
    ]

    passed = 0
    for test in tests:
        if run_test_isolated(test): passed += 1

    print(f"\n=== INTEGRATION SUMMARY: {passed}/{len(tests)} PASSED ===")
    sys.exit(0 if passed == len(tests) else 1)


if __name__ == "__main__":
    run_suite()
