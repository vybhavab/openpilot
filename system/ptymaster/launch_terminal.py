#!/usr/bin/env python3
"""
Terminal launcher for Comma 3
Starts both PTY service and terminal display
"""
import os
import sys
import time
import signal
import subprocess
import threading
from pathlib import Path


class TerminalLauncher:
    def __init__(self, session_id: str = "default"):
        self.session_id = session_id
        self.pty_service_process = None
        self.terminal_app_process = None
        self.running = True
        
        # Set up signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        print(f"Received signal {signum}, shutting down...")
        self.stop()
    
    def start_pty_service(self):
        """Start the PTY service"""
        pty_service_path = Path(__file__).parent / "pty_service.py"
        
        try:
            self.pty_service_process = subprocess.Popen([
                sys.executable, str(pty_service_path)
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            
            print("PTY service started")
            
            # Wait a moment for service to initialize
            time.sleep(1)
            
            return True
        except Exception as e:
            print(f"Failed to start PTY service: {e}")
            return False
    
    def start_terminal_app(self):
        """Start the terminal display application"""
        terminal_app_path = Path(__file__).parent.parent / "ui" / "terminal_app.py"
        
        try:
            self.terminal_app_process = subprocess.Popen([
                sys.executable, str(terminal_app_path), self.session_id
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            
            print("Terminal app started")
            return True
        except Exception as e:
            print(f"Failed to start terminal app: {e}")
            return False
    
    def monitor_processes(self):
        """Monitor and restart processes if they die"""
        while self.running:
            # Check PTY service
            if self.pty_service_process and self.pty_service_process.poll() is not None:
                print("PTY service died, restarting...")
                self.start_pty_service()
            
            # Check terminal app
            if self.terminal_app_process and self.terminal_app_process.poll() is not None:
                print("Terminal app died, restarting...")
                self.start_terminal_app()
            
            time.sleep(2)
    
    def start(self):
        """Start the complete terminal system"""
        print(f"Starting terminal system for session: {self.session_id}")
        
        # Start PTY service first
        if not self.start_pty_service():
            return False
        
        # Start terminal app
        if not self.start_terminal_app():
            self.stop()
            return False
        
        # Start monitoring thread
        monitor_thread = threading.Thread(target=self.monitor_processes, daemon=True)
        monitor_thread.start()
        
        print("Terminal system started successfully")
        return True
    
    def stop(self):
        """Stop the terminal system"""
        self.running = False
        
        # Stop terminal app
        if self.terminal_app_process:
            try:
                self.terminal_app_process.terminate()
                self.terminal_app_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.terminal_app_process.kill()
            except Exception as e:
                print(f"Error stopping terminal app: {e}")
        
        # Stop PTY service
        if self.pty_service_process:
            try:
                self.pty_service_process.terminate()
                self.pty_service_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.pty_service_process.kill()
            except Exception as e:
                print(f"Error stopping PTY service: {e}")
        
        print("Terminal system stopped")
    
    def wait(self):
        """Wait for processes to complete"""
        try:
            while self.running:
                if self.terminal_app_process:
                    self.terminal_app_process.wait()
                time.sleep(1)
        except KeyboardInterrupt:
            pass


def create_ssh_helper_script():
    """Create helper script for SSH access"""
    script_content = '''#!/bin/bash
# SSH Terminal Helper Script
# This script connects to the PTY terminal on Comma 3

SESSION_ID=${1:-default}
SOCKET_PATH="/tmp/ptymaster.sock"

echo "Connecting to terminal session: $SESSION_ID"

# Check if PTY service is running
if [ ! -S "$SOCKET_PATH" ]; then
    echo "Error: PTY service not running on Comma 3"
    echo "Please start the terminal system first:"
    echo "  python3 /data/openpilot/system/ptymaster/launch_terminal.py"
    exit 1
fi

# Connect using Python client
python3 -c "
import sys
sys.path.append('/data/openpilot')
from system.ptymaster.session_manager import PTYClient
import select

client = PTYClient()

def on_data(data):
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()

client.set_data_callback(on_data)

if client.connect('$SESSION_ID'):
    print('Connected to terminal session: $SESSION_ID', file=sys.stderr)
    
    try:
        while True:
            # Check for input from stdin
            if select.select([sys.stdin], [], [], 0.1)[0]:
                line = sys.stdin.readline()
                if not line:
                    break
                client.send_input(line)
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()
else:
    print('Failed to connect to PTY service', file=sys.stderr)
    sys.exit(1)
"
'''
    
    script_path = "/tmp/ssh_terminal.sh"
    with open(script_path, 'w') as f:
        f.write(script_content)
    
    os.chmod(script_path, 0o755)
    print(f"Created SSH helper script: {script_path}")
    print(f"Usage: ssh user@comma3 '{script_path} [session_id]'")


def main():
    """Main entry point"""
    session_id = "default"
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "--create-ssh-script":
            create_ssh_helper_script()
            return
        else:
            session_id = sys.argv[1]
    
    launcher = TerminalLauncher(session_id)
    
    if launcher.start():
        try:
            launcher.wait()
        except KeyboardInterrupt:
            pass
        finally:
            launcher.stop()
    else:
        print("Failed to start terminal system")
        sys.exit(1)


if __name__ == "__main__":
    main()