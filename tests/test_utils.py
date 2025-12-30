import socket
import subprocess
import time
import sys
import os
import shutil

# Configuration
SERVER_EXE = "./dynamic_periodic_task"
PORT = 8080
HOST = "127.0.0.1"

def get_server_command():
    """
    Constructs the execution command.
    Wraps the executable with Valgrind if available to detect memory leaks
    and invalid memory accesses during testing.
    """
    if not os.path.exists(SERVER_EXE):
        print(f"Error: Executable {SERVER_EXE} not found.")
        sys.exit(1)

    valgrind_path = shutil.which("valgrind")

    if valgrind_path:
        print(f"[test_utils] Valgrind detected. Running with memory checks.")
        return [
            valgrind_path,
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--track-origins=yes",
            "--error-exitcode=1",  # Forces failure on memory errors
            SERVER_EXE
        ]
    else:
        print(f"[test_utils] Valgrind not found. Running natively.")
        return [SERVER_EXE]

def wait_for_server(timeout=5.0):
    """
    Polls the target port until the server is ready to accept connections.
    This prevents race conditions where tests attempt to connect before
    the server has finished its initialization (e.g., CPU calibration).
    """
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.1)
            sock.connect((HOST, PORT))
            sock.close()
            return True
        except (ConnectionRefusedError, socket.timeout, OSError):
            time.sleep(0.1)
    return False

def send_command(sock, cmd):
    """
    Helper function to send a command line and retrieve the trimmed response.
    """
    try:
        sock.sendall(f"{cmd}\n".encode())
        data = sock.recv(1024).decode().strip()
        return data
    except (BrokenPipeError, ConnectionResetError):
        return ""

def stop_server_gracefully(proc):
    """
    Initiates a graceful shutdown to allow the server to clean up resources
    (join threads, close sockets). Falls back to SIGTERM/SIGKILL if unresponsive.
    """
    if not proc: return

    stopped_gracefully = False

    # Attempt 1: Send 'SHUTDOWN' command via TCP
    if proc.poll() is None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            sock.connect((HOST, PORT))
            sock.sendall(b"SHUTDOWN\n")

            # Consume potential response to ensure processing
            try:
                _ = sock.recv(1024)
            except:
                pass

            sock.close()

            # Wait for the process to exit voluntarily
            try:
                proc.wait(timeout=2.0)
                stopped_gracefully = True
            except subprocess.TimeoutExpired:
                pass
        except Exception:
            # Network might be unreachable or server crashed
            pass

    # Attempt 2: Force Kill (SIGTERM -> SIGKILL)
    if proc.poll() is None:
        if not stopped_gracefully:
            pass # Logging suppressed for cleaner output

        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

def run_test_isolated(test_func):
    """
    Test runner wrapper.
    Ensures a fresh server instance for each test case to prevent
    state contamination between tests.
    """
    cmd = get_server_command()
    is_valgrind = "valgrind" in cmd[0]

    proc = None
    logic_passed = False

    try:
        # Start Server
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # Wait for the server to bind the port
        if not wait_for_server(timeout=10.0 if is_valgrind else 3.0):
            print(f"FAIL: Server startup timed out (Valgrind: {is_valgrind})")
            return False

        # Early crash detection
        if proc.poll() is not None:
            print(f"FAIL: Server died immediately (Exit code: {proc.returncode})")
            return False

        # Execute the test logic
        print(f"\n=== RUNNING: {test_func.__name__} ===")
        logic_passed = test_func()

        # Late crash detection
        if proc.poll() is not None:
            print(f"FAIL: Server crashed during test (Exit code: {proc.returncode})")
            return False

    except Exception as e:
        print(f"CRASH/EXCEPT wrapper: {e}")
        return False

    finally:
        valgrind_error = False
        if proc:
            stop_server_gracefully(proc)

            # Check Valgrind exit code (1 indicates memory errors)
            if is_valgrind and proc.returncode == 1:
                print(f"FAIL: Valgrind detected Memory Leaks/Errors!")
                valgrind_error = True

    if logic_passed and not valgrind_error:
        print(f"PASS: {test_func.__name__}")
        return True
    else:
        print(f"FAIL: {test_func.__name__}")
        return False