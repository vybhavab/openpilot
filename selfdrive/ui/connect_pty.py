#!/usr/bin/env python3
"""
Simple SSH client to connect to the PTY server running on Comma 3 device.
This script provides a way to connect to the PTY terminal from a remote machine.
"""

import socket
import sys
import select
import termios
import tty
import threading
import signal

class PTYClient:
    def __init__(self, host, port=2222):
        self.host = host
        self.port = port
        self.socket = None
        self.running = False
        self.old_settings = None
        
    def connect(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            print(f"Connected to PTY server at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"Failed to connect: {e}")
            return False
    
    def setup_terminal(self):
        """Set terminal to raw mode for proper PTY interaction"""
        self.old_settings = termios.tcgetattr(sys.stdin)
        tty.setraw(sys.stdin.fileno())
    
    def restore_terminal(self):
        """Restore terminal settings"""
        if self.old_settings:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.old_settings)
    
    def handle_input(self):
        """Handle keyboard input and send to PTY server"""
        while self.running:
            try:
                ready, _, _ = select.select([sys.stdin], [], [], 0.1)
                if ready:
                    data = sys.stdin.read(1)
                    if data:
                        self.socket.send(data.encode())
            except Exception as e:
                print(f"Input error: {e}")
                break
    
    def handle_output(self):
        """Handle output from PTY server and display locally"""
        while self.running:
            try:
                ready, _, _ = select.select([self.socket], [], [], 0.1)
                if ready:
                    data = self.socket.recv(4096)
                    if data:
                        sys.stdout.write(data.decode('utf-8', errors='ignore'))
                        sys.stdout.flush()
                    else:
                        break
            except Exception as e:
                print(f"Output error: {e}")
                break
    
    def run(self):
        """Main client loop"""
        if not self.connect():
            return False
        
        self.running = True
        self.setup_terminal()
        
        # Set up signal handler for clean exit
        def signal_handler(sig, frame):
            self.running = False
        
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        try:
            # Start input and output threads
            input_thread = threading.Thread(target=self.handle_input)
            output_thread = threading.Thread(target=self.handle_output)
            
            input_thread.daemon = True
            output_thread.daemon = True
            
            input_thread.start()
            output_thread.start()
            
            print("PTY session started. Press Ctrl+C to exit.")
            
            # Wait for threads to finish
            while self.running:
                if not input_thread.is_alive() or not output_thread.is_alive():
                    break
                threading.Event().wait(0.1)
                
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            self.restore_terminal()
            if self.socket:
                self.socket.close()
            print("\nPTY session ended.")
        
        return True

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 connect_pty.py <comma3_ip_address>")
        print("Example: python3 connect_pty.py 192.168.1.100")
        sys.exit(1)
    
    host = sys.argv[1]
    client = PTYClient(host)
    
    if not client.run():
        sys.exit(1)

if __name__ == "__main__":
    main()