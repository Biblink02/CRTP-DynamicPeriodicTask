import socket
import time
import sys
import threading
from test_utils import run_test_isolated, HOST, PORT

def test_fuzzing_garbage():
    """
    Stress-tests the protocol parser.
    Sends massive strings, binary data, and delimiters to ensure buffer handling
    does not cause segmentation faults or buffer overflows.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        # 1. Buffer Overflow Attempt (Huge Payload)
        sock.sendall(("ACTIVATE " + "A" * 4000).encode())

        # 2. Binary Fuzzing (Bad bytes)
        sock.sendall(b"\x00\xFF\x01\x02 ACTIVATE t1 \n \r \x00")

        # 3. Delimiter Spam
        sock.sendall(b"\n\n\n\n\r\n")
        sock.close()

        # Verification: Server must still answer a valid request
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(2.0)
        sock2.connect((HOST, PORT))
        sock2.sendall(b"ACTIVATE t1\n")
        resp = sock2.recv(1024).decode()
        sock2.close()
        return "OK" in resp or "ERR" in resp
    except Exception as e:
        print(f"Server died during fuzzing: {e}")
        return False

def test_connection_storm():
    """
    Simulates high-concurrency connection attempts.
    Verifies that the accept() loop correctly handles backlog and
    does not leave sockets in a zombie state.
    """
    connections = []
    n_threads = 50  # Matches or exceeds server MAX_CLIENTS
    success_count = 0
    lock = threading.Lock()

    def connect_worker():
        nonlocal success_count
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((HOST, PORT))
            with lock:
                connections.append(s)
                success_count += 1
            time.sleep(0.5)
        except:
            pass

    threads = [threading.Thread(target=connect_worker) for _ in range(n_threads)]
    for t in threads: t.start()
    for t in threads: t.join()

    for c in connections: c.close()

    print(f"   Opened {success_count} concurrent connections.")
    if success_count == 0: return False

    # Verification: Server must still be reachable
    try:
        check = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        check.connect((HOST, PORT))
        check.close()
        return True
    except:
        return False

def test_queue_overflow():
    """
    Floods the command queue faster than the supervisor can process them.
    Verifies that the fixed-size event queue handles overflow gracefully (dropping events)
    without crashing.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        # Send 100 commands in a single burst (exceeds queue size of 20)
        sock.sendall(("ACTIVATE t1\n" * 100).encode())

        sock.settimeout(2.0)
        responses = ""
        try:
            while True:
                chunk = sock.recv(4096).decode()
                if not chunk: break
                responses += chunk
        except socket.timeout:
            pass
        sock.close()

        total_replies = responses.count("OK") + responses.count("ERR")
        print(f"   Sent 100 cmds. Received {total_replies} replies.")
        if total_replies == 0: return False

        # Verification: Vitality check
        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect((HOST, PORT))
        s2.close()
        return True
    except:
        return False

def test_rapid_churn_cycle():
    """
    Stress-tests memory management and ID allocation logic.
    Rapidly activates and deactivates tasks to ensure memory is correctly freed
    and thread resources are reclaimed (pthread_join validation).
    """
    print("\n=== RUNNING: test_rapid_churn_cycle ===")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        sock.connect((HOST, PORT))
        iterations = 100

        for i in range(iterations):
            sock.sendall(b"a t1\n")
            resp = sock.recv(1024).decode()
            if "ID=" not in resp: return False
            try:
                tid = int(resp.strip().split("ID=")[1])
            except: return False

            sock.sendall(f"d {tid}\n".encode())
            resp_deact = sock.recv(1024).decode()
            if "OK" not in resp_deact: return False

        sock.close()
        print(f"   Completed {iterations} cycles successfully.")
        return True
    except Exception as e:
        print(f"CRASH/EXCEPT in churn test: {e}")
        return False

def test_rapid_connect_disconnect():
    """
    Tests the robustness of the I/O multiplexing loop.
    Rapidly opens and closes connections to ensure file descriptors are
    correctly closed and removed from the polling set.
    """
    try:
        for i in range(50):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((HOST, PORT))
            # Inject garbage occasionally to test parser resilience
            if i % 2 == 0: s.sendall(b"GARBAGE")
            s.close()

        # Verification: Server must accept a new valid connection
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        s.sendall(b"ACTIVATE t1\n")
        res = s.recv(1024)
        s.close()
        return b"OK" in res or b"ERR" in res
    except:
        return False

if __name__ == "__main__":
    tests = [
        test_fuzzing_garbage,
        test_queue_overflow,
        test_connection_storm,
        test_rapid_connect_disconnect,
        test_rapid_churn_cycle
    ]

    passed = 0
    for test in tests:
        if run_test_isolated(test): passed += 1

    print(f"\nSTRESS TEST SUMMARY: {passed}/{len(tests)} Passed")
    sys.exit(0 if passed == len(tests) else 1)