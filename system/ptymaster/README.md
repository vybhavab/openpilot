# PTY Terminal System for Comma 3

This system provides a PTY (pseudo-terminal) slave/master session that displays on the Comma 3 screen, allowing multiple SSH connections to share the same terminal session.

## Components

### 1. PTY Service (`pty_service.py`)
- Creates and manages pseudo-terminal sessions
- Handles multiple client connections to the same PTY
- Provides Unix socket interface for communication
- Supports terminal resizing and proper signal handling

### 2. Session Manager (`session_manager.py`)
- Client library for connecting to PTY service
- Handles SSH session bridging
- Provides helper scripts for easy SSH access

### 3. Terminal Widget (`../ui/widgets/terminal.py`)
- VT100/ANSI escape sequence compatible terminal emulator
- Integrates with openpilot's Raylib UI framework
- Supports colors, cursor positioning, and scrollback

### 4. Terminal Application (`../ui/terminal_app.py`)
- Full-screen terminal display application
- Handles keyboard input and display rendering
- Connects to PTY service for terminal I/O

### 5. Launcher (`launch_terminal.py`)
- Starts and manages both PTY service and terminal display
- Monitors processes and restarts them if they crash
- Provides unified startup/shutdown

## Usage

### Starting the Terminal System

1. **On the Comma 3 device:**
   ```bash
   cd /data/openpilot
   python3 system/ptymaster/launch_terminal.py [session_id]
   ```

2. **Create SSH helper script:**
   ```bash
   python3 system/ptymaster/launch_terminal.py --create-ssh-script
   ```

### Connecting via SSH

1. **Using the helper script:**
   ```bash
   ssh user@comma3 '/tmp/ssh_terminal.sh [session_id]'
   ```

2. **Manual connection:**
   ```bash
   ssh user@comma3
   python3 /data/openpilot/system/ptymaster/session_manager.py
   ```

### Multiple SSH Sessions

Multiple SSH sessions can connect to the same terminal session by using the same `session_id`. All sessions will see the same terminal output and can provide input.

## Features

### Terminal Emulation
- **VT100/ANSI Compatibility**: Supports standard escape sequences
- **Colors**: 16-color ANSI color support
- **Cursor Control**: Full cursor positioning and movement
- **Screen Control**: Clear screen, clear line operations
- **Scrollback**: 1000-line scrollback buffer

### Display
- **Full Screen**: Uses entire Comma 3 display (2160x1080)
- **Monospace Font**: Proper terminal character alignment
- **Cursor Blinking**: Visual cursor indication
- **Real-time Updates**: 60 FPS display refresh

### Session Management
- **Multiple Sessions**: Support for multiple named sessions
- **Session Sharing**: Multiple SSH clients per session
- **Auto-restart**: Automatic process recovery
- **Clean Shutdown**: Proper resource cleanup

## Configuration

### Terminal Size
The terminal automatically calculates size based on display dimensions:
- **Default**: ~80 columns x 24 rows
- **Auto-resize**: Adjusts when display changes
- **SSH Negotiation**: Proper terminal size reporting

### Environment Variables
- `TERM=xterm-256color`: Set for proper terminal compatibility
- `PS1`: Custom prompt for Comma 3 identification

## File Structure

```
system/ptymaster/
├── __init__.py
├── pty_service.py          # PTY master service
├── session_manager.py      # Client library and SSH bridge
├── launch_terminal.py      # System launcher
└── README.md              # This file

system/ui/widgets/
└── terminal.py            # Terminal emulator widget

system/ui/
└── terminal_app.py        # Terminal display application
```

## Dependencies

- **Python 3.8+**: Core runtime
- **pyray**: Graphics and input handling
- **Standard Library**: pty, socket, threading, select

## Troubleshooting

### PTY Service Not Starting
- Check if socket `/tmp/ptymaster.sock` exists
- Verify permissions on socket file
- Check system logs for error messages

### Terminal Display Issues
- Ensure Raylib dependencies are installed
- Check display permissions and hardware access
- Verify font files are available

### SSH Connection Problems
- Confirm PTY service is running
- Check network connectivity to Comma 3
- Verify SSH access and permissions

### Performance Issues
- Monitor CPU usage during operation
- Check for memory leaks in long-running sessions
- Adjust terminal size if rendering is slow

## Security Considerations

- **Local Access Only**: PTY service binds to Unix socket
- **No Authentication**: Relies on SSH for access control
- **Process Isolation**: Each session runs in separate process
- **Resource Limits**: Automatic cleanup of dead sessions

## Future Enhancements

- **Session Persistence**: Save/restore terminal sessions
- **Multiple Terminals**: Tabbed or windowed interface
- **File Transfer**: Built-in file upload/download
- **Remote Display**: VNC-like remote terminal access
- **Integration**: Direct integration with openpilot UI system