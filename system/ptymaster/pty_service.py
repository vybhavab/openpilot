#!/usr/bin/env python3
import os
import pty
import select
import socket
import threading
import time
import signal
import struct
import termios
import fcntl
from typing import Dict, List, Optional
import json

class PTYSession:
    def __init__(self, session_id: str):
        self.session_id = session_id
        self.master_fd, self.slave_fd = pty.openpty()
        self.clients: List[socket.socket] = []
        self.running = True
        self.lock = threading.Lock()
        self.shell_pid: Optional[int] = None
        
        # Set up terminal attributes
        self._setup_terminal()
        
        # Start shell process
        self._start_shell()
        
        # Start I/O threads
        self.read_thread = threading.Thread(target=self._read_from_pty, daemon=True)
        self.read_thread.start()
    
    def _setup_terminal(self):
        """Configure terminal attributes for proper operation"""
        # Set terminal size (80x24 default, will be updated by clients)
        winsize = struct.pack('HHHH', 24, 80, 0, 0)
        fcntl.ioctl(self.slave_fd, termios.TIOCSWINSZ, winsize)
        
        # Configure terminal attributes
        attrs = termios.tcgetattr(self.slave_fd)
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                     termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
        attrs[1] &= ~termios.OPOST
        attrs[2] &= ~(termios.CSIZE | termios.PARENB)
        attrs[2] |= termios.CS8
        attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.slave_fd, termios.TCSANOW, attrs)
    
    def _start_shell(self):
        """Start shell process attached to PTY slave"""
        self.shell_pid = os.fork()
        if self.shell_pid == 0:
            # Child process
            os.setsid()
            os.dup2(self.slave_fd, 0)
            os.dup2(self.slave_fd, 1)
            os.dup2(self.slave_fd, 2)
            os.close(self.master_fd)
            os.close(self.slave_fd)
            
            # Set environment variables
            os.environ['TERM'] = 'xterm-256color'
            os.environ['PS1'] = r'\u@comma3:\w\$ '
            
            # Execute shell
            os.execv('/bin/bash', ['/bin/bash', '-l'])
    
    def _read_from_pty(self):
        """Read data from PTY master and broadcast to all clients"""
        while self.running:
            try:
                ready, _, _ = select.select([self.master_fd], [], [], 0.1)
                if ready:
                    data = os.read(self.master_fd, 4096)
                    if data:
                        with self.lock:
                            for client in self.clients[:]:
                                try:
                                    client.send(data)
                                except (BrokenPipeError, ConnectionResetError):
                                    self.clients.remove(client)
                                    client.close()
            except OSError:
                break
    
    def add_client(self, client_socket: socket.socket):
        """Add a new client to this PTY session"""
        with self.lock:
            self.clients.append(client_socket)
    
    def remove_client(self, client_socket: socket.socket):
        """Remove a client from this PTY session"""
        with self.lock:
            if client_socket in self.clients:
                self.clients.remove(client_socket)
                client_socket.close()
    
    def write_to_pty(self, data: bytes):
        """Write data to PTY master (from client input)"""
        try:
            os.write(self.master_fd, data)
        except OSError:
            pass
    
    def resize_terminal(self, rows: int, cols: int):
        """Resize the terminal"""
        winsize = struct.pack('HHHH', rows, cols, 0, 0)
        try:
            fcntl.ioctl(self.slave_fd, termios.TIOCSWINSZ, winsize)
            if self.shell_pid:
                os.kill(self.shell_pid, signal.SIGWINCH)
        except OSError:
            pass
    
    def close(self):
        """Close the PTY session"""
        self.running = False
        
        # Close all client connections
        with self.lock:
            for client in self.clients[:]:
                client.close()
            self.clients.clear()
        
        # Terminate shell process
        if self.shell_pid:
            try:
                os.kill(self.shell_pid, signal.SIGTERM)
                os.waitpid(self.shell_pid, 0)
            except (OSError, ProcessLookupError):
                pass
        
        # Close PTY file descriptors
        try:
            os.close(self.master_fd)
            os.close(self.slave_fd)
        except OSError:
            pass


class PTYService:
    def __init__(self, socket_path: str = "/tmp/ptymaster.sock"):
        self.socket_path = socket_path
        self.sessions: Dict[str, PTYSession] = {}
        self.running = True
        self.server_socket: Optional[socket.socket] = None
        
        # Clean up any existing socket
        try:
            os.unlink(socket_path)
        except FileNotFoundError:
            pass
    
    def start(self):
        """Start the PTY service"""
        self.server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_socket.bind(self.socket_path)
        self.server_socket.listen(5)
        
        print(f"PTY service listening on {self.socket_path}")
        
        while self.running:
            try:
                client_socket, _ = self.server_socket.accept()
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_socket,),
                    daemon=True
                )
                client_thread.start()
            except OSError:
                break
    
    def _handle_client(self, client_socket: socket.socket):
        """Handle a client connection"""
        session_id = None
        session = None
        
        try:
            while True:
                # Receive message from client
                data = client_socket.recv(4096)
                if not data:
                    break
                
                try:
                    message = json.loads(data.decode('utf-8'))
                except (json.JSONDecodeError, UnicodeDecodeError):
                    # Raw data, send to PTY if we have a session
                    if session:
                        session.write_to_pty(data)
                    continue
                
                msg_type = message.get('type')
                
                if msg_type == 'connect':
                    session_id = message.get('session_id', 'default')
                    
                    # Create session if it doesn't exist
                    if session_id not in self.sessions:
                        self.sessions[session_id] = PTYSession(session_id)
                    
                    session = self.sessions[session_id]
                    session.add_client(client_socket)
                    
                    # Send confirmation
                    response = {'type': 'connected', 'session_id': session_id}
                    client_socket.send(json.dumps(response).encode('utf-8') + b'\n')
                
                elif msg_type == 'resize' and session:
                    rows = message.get('rows', 24)
                    cols = message.get('cols', 80)
                    session.resize_terminal(rows, cols)
                
                elif msg_type == 'input' and session:
                    input_data = message.get('data', '').encode('utf-8')
                    session.write_to_pty(input_data)
        
        except (ConnectionResetError, BrokenPipeError):
            pass
        finally:
            if session and client_socket:
                session.remove_client(client_socket)
    
    def get_session(self, session_id: str) -> Optional[PTYSession]:
        """Get a PTY session by ID"""
        return self.sessions.get(session_id)
    
    def create_session(self, session_id: str) -> PTYSession:
        """Create a new PTY session"""
        if session_id in self.sessions:
            return self.sessions[session_id]
        
        session = PTYSession(session_id)
        self.sessions[session_id] = session
        return session
    
    def close_session(self, session_id: str):
        """Close a PTY session"""
        if session_id in self.sessions:
            self.sessions[session_id].close()
            del self.sessions[session_id]
    
    def stop(self):
        """Stop the PTY service"""
        self.running = False
        
        # Close all sessions
        for session in self.sessions.values():
            session.close()
        self.sessions.clear()
        
        # Close server socket
        if self.server_socket:
            self.server_socket.close()
        
        # Clean up socket file
        try:
            os.unlink(self.socket_path)
        except FileNotFoundError:
            pass


def main():
    """Main entry point for PTY service"""
    service = PTYService()
    
    def signal_handler(signum, frame):
        print("Shutting down PTY service...")
        service.stop()
        exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        service.start()
    except KeyboardInterrupt:
        service.stop()


if __name__ == "__main__":
    main()