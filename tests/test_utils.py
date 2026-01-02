import socket
import subprocess
import time
import sys
import os
import shutil

SERVER_EXE = "./dynamic_periodic_task"
PORT = 8080
HOST = "127.0.0.1"

def log(msg):
    print(f"[TEST-DEBUG] {msg}", flush=True)

def get_server_command():
    if not os.path.exists(SERVER_EXE):
        log(f"Error: Executable {SERVER_EXE} not found.")
        sys.exit(1)

    valgrind = shutil.which("valgrind")
    if valgrind:
        # Full memory checks enabled
        log("Valgrind detected: Running with memory checks (this will be slow).")
        return [
            valgrind,
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--track-origins=yes",
            "--error-exitcode=1",
            SERVER_EXE
        ]
    return [SERVER_EXE]

def wait_for_server(timeout=5.0):
    start = time.time()
    while time.time() - start < timeout:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            sock.connect((HOST, PORT))
            sock.close()
            return True
        except:
            time.sleep(0.1)
    return False

def send_command(sock, cmd):
    try:
        log(f"Sending: {cmd}")
        sock.sendall(f"{cmd}\n".encode())
        resp = sock.recv(1024).decode().strip()
        log(f"Received: {resp}")
        return resp
    except Exception as e:
        log(f"Socket Error during send/recv: {e}")
        return f"ERR {e}"

def run_test_isolated(test_func):
    log(f"Starting Server for {test_func.__name__}...")

    cmd = get_server_command()
    is_valgrind = "valgrind" in cmd[0]

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Valgrind starts much slower
    startup_timeout = 20.0 if is_valgrind else 5.0
    if not wait_for_server(startup_timeout):
        log("FAIL: Server startup timed out (Port not open)")
        proc.terminate()
        return False

    # CRITICAL: Wait for CPU Calibration to finish
    # Without Valgrind: 1s is enough. With Valgrind: need 5s+.
    wait_time = 5.0 if is_valgrind else 1.0
    log(f"Waiting {wait_time}s for Server Calibration (Valgrind={is_valgrind})...")
    time.sleep(wait_time)

    log(f"=== RUNNING: {test_func.__name__} ===")

    try:
        result = test_func()
    except Exception as e:
        log(f"EXCEPTION in test wrapper: {e}")
        result = False

    log(f"Stopping Server...")
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except:
        proc.kill()

    # Check for memory leaks if Valgrind was used
    if result and is_valgrind and proc.returncode == 1:
        log(f"FAIL: Logic passed, but Valgrind found MEMORY ERRORS!")
        result = False

    if result:
        log(f"PASS: {test_func.__name__}")
    else:
        log(f"FAIL: {test_func.__name__}")
    return result