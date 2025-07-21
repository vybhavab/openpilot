#!/usr/bin/env python3
import socket
import sys
import select
import termios
import tty
import threading

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 simple_pty_client.py <host>")
        sys.exit(1)
    
    host = sys.argv[1]
    port = 2222
    
    # Connect to server
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
        print(f"Connected to {host}:{port}")
    except Exception as e:
        print(f"Connection failed: {e}")
        sys.exit(1)
    
    # Save terminal settings
    old_settings = termios.tcgetattr(sys.stdin)
    tty.setraw(sys.stdin.fileno())
    
    running = True
    
    def read_from_server():
        nonlocal running
        while running:
            try:
                data = sock.recv(4096)
                if not data:
                    break
                sys.stdout.write(data.decode('utf-8', errors='ignore'))
                sys.stdout.flush()
            except:
                break
        running = False
    
    # Start thread to read from server
    thread = threading.Thread(target=read_from_server)
    thread.daemon = True
    thread.start()
    
    try:
        while running:
            # Read from stdin and send to server
            ready, _, _ = select.select([sys.stdin], [], [], 0.1)
            if ready:
                data = sys.stdin.read(1)
                if data:
                    sock.send(data.encode())
    except KeyboardInterrupt:
        pass
    finally:
        running = False
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
        sock.close()
        print("\nDisconnected")

if __name__ == "__main__":
    main()