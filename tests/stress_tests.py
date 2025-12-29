import socket
import subprocess
import time
import sys
import os
import threading
import random
import string
import shutil

# Configuration
SERVER_EXE = "./dynamic_periodic_task"
PORT = 8080
HOST = "127.0.0.1"


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


def run_isolated(test_func):
    """Executes a stress test with strict crash and memory checking."""
    cmd = get_server_command()
    is_valgrind = "valgrind" in cmd[0]

    proc = None
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5 if is_valgrind else 0.5)

        if proc.poll() is not None:
            print(f"FAIL: Server died immediately (Exit code: {proc.returncode})")
            return False

        print(f"\n=== RUNNING: {test_func.__name__} ===")
        logic_passed = test_func()

        if proc.poll() is not None:
            print(f"FAIL: Server crashed during test (Exit code: {proc.returncode})")
            return False

    except Exception as e:
        print(f"CRASH/EXCEPT: {e}")
        return False

    finally:
        valgrind_error = False
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

            if is_valgrind and proc.returncode == 1:
                print(f"FAIL: Valgrind detected Memory Leaks/Errors!")
                valgrind_error = True

    if logic_passed and not valgrind_error:
        print(f"PASS: {test_func.__name__}")
        return True
    else:
        print(f"FAIL: {test_func.__name__}")
        return False


#  STRESS SCENARIOS

def test_fuzzing_garbage():
    """Sends binary data, huge strings and malformed commands."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        # Huge Payload
        huge_payload = "ACTIVATE " + "A" * 4000
        sock.sendall(huge_payload.encode())

        # Null chars and binary
        bad_bytes = b"\x00\xFF\x01\x02 ACTIVATE t1 \n \r \x00"
        sock.sendall(bad_bytes)

        # Newline spam
        sock.sendall(b"\n\n\n\n\r\n")
        sock.close()

        # Check server vitality
        time.sleep(0.2)
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.connect((HOST, PORT))
        sock2.sendall(b"ACTIVATE t1\n")
        resp = sock2.recv(1024).decode()
        sock2.close()
        return "OK" in resp or "ERR" in resp
    except Exception as e:
        print(f"Server died during fuzzing: {e}")
        return False


def test_connection_storm():
    """Attempts to open many concurrent connections."""
    connections = []
    n_threads = 50
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

    # Check server vitality
    try:
        check = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        check.connect((HOST, PORT))
        check.close()
        return True
    except:
        return False


def test_queue_overflow():
    """Spams commands to fill the event queue."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        burst = "ACTIVATE t1\n" * 100
        sock.sendall(burst.encode())

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

        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect((HOST, PORT))
        s2.close()
        return True
    except Exception as e:
        return False


def test_rapid_connect_disconnect():
    """Rapidly opens and closes connections."""
    try:
        cycles = 50
        for i in range(cycles):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((HOST, PORT))
            if i % 2 == 0: s.sendall(b"GARBAGE")
            s.close()

        # Final Check
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        s.sendall(b"ACTIVATE t1\n")
        res = s.recv(1024)
        s.close()
        return b"OK" in res or b"ERR" in res
    except:
        return False


#  MAIN

if __name__ == "__main__":
    tests = [
        test_fuzzing_garbage,
        test_queue_overflow,
        test_connection_storm,
        test_rapid_connect_disconnect
    ]

    passed = 0
    for t in tests:
        if run_isolated(t): passed += 1

    print(f"\nSTRESS TEST SUMMARY: {passed}/{len(tests)} Passed")
    sys.exit(0 if passed == len(tests) else 1)
