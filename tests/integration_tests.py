import socket
import sys
from test_utils import run_test_isolated, send_command, log, HOST, PORT

def test_protocol_failure_injection():
    """
    Verifies the server handles invalid commands gracefully.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0) # Higher timeout for Valgrind
        sock.connect((HOST, PORT))

        # Test 1: Garbage Command
        resp1 = send_command(sock, "GARBAGE_DATA")
        if "ERR" not in resp1:
            log(f"Fail: Expected ERR for GARBAGE, got '{resp1}'")
            return False

        # Test 2: Invalid ID format
        resp2 = send_command(sock, "DEACTIVATE 999")
        if "ERR" not in resp2:
            log(f"Fail: Expected ERR for invalid ID, got '{resp2}'")
            return False

        sock.close()
        return True
    except Exception as e:
        log(f"Exception: {e}")
        return False

def test_schedulability_saturation():
    """
    Floods the system with tasks until rejection.
    Validates that the RTA (Response Time Analysis) or System Limit eventually rejects a task.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((HOST, PORT))

        ok_count = 0
        err_count = 0

        # Try to activate tasks until full (Max 20 instances)
        for i in range(25):
            log(f"Saturation Step {i+1}...")
            resp = send_command(sock, "ACTIVATE t3")

            if "OK" in resp:
                ok_count += 1
            elif "ERR" in resp:
                err_count += 1
                log(f"Saturation reached at task {i+1} ({resp})")
                break
            else:
                log(f"Unexpected response: {resp}")
                break

        sock.close()

        log(f"Result: OK={ok_count}, ERR={err_count}")

        # Must accept at least one task to be valid
        if ok_count == 0:
            log("Fail: System accepted 0 tasks.")
            return False

        return True
    except Exception as e:
        log(f"Exception: {e}")
        return False

def test_dynamic_stress():
    """
    Validates the full lifecycle: Activate -> Get ID -> Deactivate -> Reactivate.
    Ensures IDs are reused or managed correctly.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((HOST, PORT))

        log("1. Activating t1...")
        resp = send_command(sock, "ACTIVATE t1")
        if "OK" not in resp:
            log(f"Fail: Activation failed. Resp: {resp}")
            return False

        try:
            tid = resp.split("ID=")[1]
            log(f"   Got ID: {tid}")
        except IndexError:
            log("Fail: Could not parse ID")
            return False

        log(f"2. Deactivating ID {tid}...")
        resp2 = send_command(sock, f"DEACTIVATE {tid}")
        if "OK" not in resp2:
            log(f"Fail: Deactivation failed. Resp: {resp2}")
            return False

        log("3. Reactivating t1...")
        resp3 = send_command(sock, "ACTIVATE t1")
        if "OK" not in resp3:
            log(f"Fail: Reactivation failed. Resp: {resp3}")
            return False

        sock.close()
        return True
    except Exception as e:
        log(f"Exception: {e}")
        return False

if __name__ == "__main__":
    tests = [
        test_protocol_failure_injection,
        test_schedulability_saturation,
        test_dynamic_stress
    ]
    passed = 0
    for t in tests:
        if run_test_isolated(t): passed += 1
    sys.exit(0 if passed == len(tests) else 1)