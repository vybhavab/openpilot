#!/usr/bin/env python3
import socket
import json
import threading
import time
from typing import Optional, Callable

class PTYClient:
    def __init__(self, socket_path: str = "/tmp/ptymaster.sock"):
        self.socket_path = socket_path
        self.socket: Optional[socket.socket] = None
        self.connected = False
        self.session_id: Optional[str] = None
        self.data_callback: Optional[Callable[[bytes], None]] = None
        self.receive_thread: Optional[threading.Thread] = None
        self.running = False
    
    def connect(self, session_id: str = "default") -> bool:
        """Connect to PTY service and join a session"""
        try:
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.connect(self.socket_path)
            
            # Send connect message
            connect_msg = {
                'type': 'connect',
                'session_id': session_id
            }
            self.socket.send(json.dumps(connect_msg).encode('utf-8'))
            
            # Wait for confirmation
            response = self.socket.recv(1024)
            try:
                response_data = json.loads(response.decode('utf-8').strip())
                if response_data.get('type') == 'connected':
                    self.connected = True
                    self.session_id = session_id
                    self.running = True
                    
                    # Start receive thread
                    self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
                    self.receive_thread.start()
                    
                    return True
            except (json.JSONDecodeError, UnicodeDecodeError):
                pass
            
            return False
        
        except (ConnectionRefusedError, FileNotFoundError):
            return False
    
    def _receive_loop(self):
        """Receive data from PTY service"""
        while self.running and self.socket:
            try:
                data = self.socket.recv(4096)
                if not data:
                    break
                
                if self.data_callback:
                    self.data_callback(data)
            
            except (ConnectionResetError, BrokenPipeError):
                break
        
        self.connected = False
    
    def send_input(self, data: str):
        """Send input to the PTY"""
        if not self.connected or not self.socket:
            return False
        
        try:
            input_msg = {
                'type': 'input',
                'data': data
            }
            self.socket.send(json.dumps(input_msg).encode('utf-8'))
            return True
        except (BrokenPipeError, ConnectionResetError):
            self.connected = False
            return False
    
    def resize_terminal(self, rows: int, cols: int):
        """Resize the terminal"""
        if not self.connected or not self.socket:
            return False
        
        try:
            resize_msg = {
                'type': 'resize',
                'rows': rows,
                'cols': cols
            }
            self.socket.send(json.dumps(resize_msg).encode('utf-8'))
            return True
        except (BrokenPipeError, ConnectionResetError):
            self.connected = False
            return False
    
    def set_data_callback(self, callback: Callable[[bytes], None]):
        """Set callback for received data"""
        self.data_callback = callback
    
    def disconnect(self):
        """Disconnect from PTY service"""
        self.running = False
        self.connected = False
        
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
        
        if self.receive_thread and self.receive_thread.is_alive():
            self.receive_thread.join(timeout=1.0)


class SSHPTYBridge:
    """Bridge for connecting SSH sessions to PTY instances"""
    
    def __init__(self, pty_socket_path: str = "/tmp/ptymaster.sock"):
        self.pty_socket_path = pty_socket_path
        self.clients: dict = {}
    
    def create_ssh_session(self, session_id: str = "default") -> PTYClient:
        """Create a new SSH session connected to a PTY"""
        client = PTYClient(self.pty_socket_path)
        
        if client.connect(session_id):
            self.clients[session_id] = client
            return client
        else:
            raise ConnectionError("Failed to connect to PTY service")
    
    def get_session(self, session_id: str) -> Optional[PTYClient]:
        """Get an existing SSH session"""
        return self.clients.get(session_id)
    
    def close_session(self, session_id: str):
        """Close an SSH session"""
        if session_id in self.clients:
            self.clients[session_id].disconnect()
            del self.clients[session_id]
    
    def close_all_sessions(self):
        """Close all SSH sessions"""
        for client in self.clients.values():
            client.disconnect()
        self.clients.clear()


def create_pty_shell_script():
    """Create a shell script for easy PTY access via SSH"""
    script_content = '''#!/bin/bash
# PTY Shell Access Script
# Usage: pty_shell.sh [session_id]

SESSION_ID=${1:-default}
SOCKET_PATH="/tmp/ptymaster.sock"

if [ ! -S "$SOCKET_PATH" ]; then
    echo "Error: PTY service not running. Socket $SOCKET_PATH not found."
    exit 1
fi

# Use socat to connect to the PTY service
exec socat STDIO UNIX-CONNECT:$SOCKET_PATH
'''
    
    with open('/tmp/pty_shell.sh', 'w') as f:
        f.write(script_content)
    
    import stat
    import os
    os.chmod('/tmp/pty_shell.sh', stat.S_IRWXU | stat.S_IRGRP | stat.S_IROTH)
    
    print("Created PTY shell script at /tmp/pty_shell.sh")
    print("Usage: ssh user@comma3 '/tmp/pty_shell.sh [session_id]'")


if __name__ == "__main__":
    # Example usage
    import sys
    
    if len(sys.argv) > 1 and sys.argv[1] == "create-script":
        create_pty_shell_script()
        sys.exit(0)
    
    # Test client
    client = PTYClient()
    
    def on_data(data):
        print(data.decode('utf-8', errors='ignore'), end='')
    
    client.set_data_callback(on_data)
    
    if client.connect("test_session"):
        print("Connected to PTY service")
        
        try:
            while True:
                user_input = input()
                client.send_input(user_input + '\n')
        except KeyboardInterrupt:
            pass
        finally:
            client.disconnect()
    else:
        print("Failed to connect to PTY service")