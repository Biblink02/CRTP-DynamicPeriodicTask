import socket
import time
import sys
import threading
from test_utils import run_test_isolated, send_command, HOST, PORT

MAX_CONCURRENT_CLIENTS = 5

def client_worker(thread_id, results, lock):
    """
    Worker thread that simulates a complete client interaction lifecycle:
    Connect -> Activate Task -> Wait -> Deactivate Task -> Disconnect.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
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
                "id": assigned_id
            })

    except Exception as e:
        with lock:
            results.append({"thread": thread_id, "status": "EXCEPTION", "error": str(e)})

def test_concurrent_clients():
    """
    Verifies thread safety by launching multiple clients simultaneously.
    Ensures that task IDs are unique and no race conditions occur during allocation.
    """
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
    """
    Tests the server's robustness against invalid inputs.
    Verifies that garbage commands and invalid IDs are handled gracefully (error response)
    instead of crashing the server.
    """
    print("\n [TEST] Failure Injection & Protocol ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        # Test 1: Send Garbage
        if "ERR" not in send_command(sock, "GARBAGE_COMMAND"): return False

        # Test 2: Deactivate non-existent ID
        if "ERR" not in send_command(sock, "DEACTIVATE 9999"): return False

        sock.close()

        # Test 3: Ensure server is still alive
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.connect((HOST, PORT))
        sock2.close()
        return True
    except:
        return False

def test_schedulability_saturation():
    """
    Validates the Response Time Analysis (RTA) logic.
    Repeatedly adds tasks until the system is full or unschedulable,
    expecting the server to eventually reject requests.
    """
    print("\n [TEST] Schedulability Saturation ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        accepted = 0

        # Task t3 has high utilization. 10 activations should trigger saturation.
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
    """
    Tests the lifecycle management of tasks.
    Activates a task, obtains an ID, deactivates it, and verifies
    that a new activation is successful (ID reuse or new allocation).
    """
    print("\n [TEST] Dynamic Stress ")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        # 1. Activate
        resp = send_command(sock, "ACTIVATE t1")
        if "OK" not in resp: return False
        tid = int(resp.split("ID=")[1])

        # 2. Deactivate
        if "OK" not in send_command(sock, f"DEACTIVATE {tid}"): return False

        # 3. Activate Again
        resp2 = send_command(sock, "ACTIVATE t1")
        if "OK" not in resp2: return False
        tid2 = int(resp2.split("ID=")[1])

        print(f"PASS: Cycle OK. IDs: {tid} -> {tid2}")
        return True
    except:
        return False

if __name__ == "__main__":
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