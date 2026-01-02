import socket
import time
import sys
from test_utils import run_test_isolated, log, HOST, PORT

def test_fuzzing_garbage():
    """
    Sends a massive payload exceeding the buffer size.
    Verifies the server detects overflow without crashing.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((HOST, PORT))

        log("Sending massive payload (buffer overflow attempt)...")
        try:
            # Send 5000 bytes (Buffer is 4096)
            sock.sendall(("ACTIVATE " + "A" * 5000).encode())
            resp = sock.recv(1024).decode()
            log(f"Response to massive payload: {resp}")
        except socket.error as e:
            log(f"Socket closed/error (expected behavior): {e}")

        sock.close()

        # Vitality Check: Is server still alive?
        log("Checking server vitality...")
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(5.0)
        sock2.connect((HOST, PORT))
        sock2.sendall(b"ACTIVATE t1\n")
        resp = sock2.recv(1024).decode()
        sock2.close()

        log(f"Vitality Check Response: {resp}")
        return "OK" in resp or "ERR" in resp
    except Exception as e:
        log(f"Exception: {e}")
        return False

def test_queue_overflow():
    """
    Floods the command queue faster than the Supervisor can process.
    Verifies that the Network thread handles full queues gracefully.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10.0)
        sock.connect((HOST, PORT))

        log("Flooding command queue (100 cmds)...")
        sock.sendall(("ACTIVATE t1\n" * 100).encode())

        received_bytes = 0
        try:
            # Drain buffer
            while received_bytes < 500:
                chunk = sock.recv(4096)
                if not chunk: break
                received_bytes += len(chunk)
        except socket.timeout:
            log("Socket timeout while draining buffer (normal).")

        log(f"Received total {received_bytes} bytes of response.")
        sock.close()

        return received_bytes > 0
    except Exception as e:
        log(f"Exception: {e}")
        return False

def test_rapid_churn_cycle():
    """
    Rapidly creates and destroys tasks to stress memory management.
    Cycles reduced to 20 to prevent Valgrind timeouts.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(15.0)
        sock.connect((HOST, PORT))

        cycles = 20

        for i in range(cycles):
            log(f"Churn Cycle {i+1}/{cycles}...")

            # Activate
            sock.sendall(b"a t1\n")
            resp = sock.recv(1024).decode()
            if "ID=" not in resp:
                log(f"Fail: No ID in response: {resp}")
                return False

            tid = resp.strip().split("ID=")[1]

            # Deactivate
            sock.sendall(f"d {tid}\n".encode())
            resp2 = sock.recv(1024).decode()
            if "OK" not in resp2:
                log(f"Fail: Deactivate failed: {resp2}")
                return False

        sock.close()
        return True
    except Exception as e:
        log(f"Churn Exception: {e}")
        return False

if __name__ == "__main__":
    tests = [test_fuzzing_garbage, test_queue_overflow, test_rapid_churn_cycle]
    passed = 0
    for t in tests:
        if run_test_isolated(t): passed += 1
    sys.exit(0 if passed == len(tests) else 1)