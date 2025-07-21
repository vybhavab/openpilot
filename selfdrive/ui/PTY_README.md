# PTY Terminal System for OpenPilot

This PTY (Pseudo Terminal) system provides a proper terminal emulator that works across SSH connections, replacing the previous tmux-based approach with a more robust master/slave PTY implementation.

## Components

### 1. `pty_terminal.cc`
A standalone PTY terminal emulator with raylib-based rendering:
- Creates a master/slave PTY pair
- Spawns a bash shell in the slave PTY
- Renders terminal output with proper ANSI escape sequence handling
- Supports colors, bold text, underline, cursor movement
- Handles keyboard input including special keys and control sequences

### 2. `ssh_pty_server.cc`
An SSH-compatible PTY server that displays terminal sessions on the Comma 3:
- Listens on port 2222 for incoming connections
- Creates a new PTY session for each client
- Displays the terminal session on the Comma 3 screen using raylib
- Forwards all I/O between the SSH client and the PTY
- Supports multiple connection attempts (one at a time)

### 3. `connect_pty.py`
A Python SSH client for connecting to the PTY server:
- Connects to the PTY server running on the Comma 3
- Sets up raw terminal mode for proper PTY interaction
- Handles bidirectional I/O between local terminal and remote PTY
- Supports proper terminal control sequences

## Building

The PTY programs are built automatically with the OpenPilot build system:

```bash
scons selfdrive/ui/pty_terminal
scons selfdrive/ui/ssh_pty_server
```

## Usage

### Option 1: Standalone PTY Terminal
Run directly on the Comma 3 device:
```bash
./selfdrive/ui/pty_terminal
```

### Option 2: SSH PTY Server (Recommended)
1. On the Comma 3 device, start the PTY server:
```bash
./selfdrive/ui/ssh_pty_server
```

2. From a remote machine, connect using the Python client:
```bash
python3 selfdrive/ui/connect_pty.py <comma3_ip_address>
```

Or use any SSH client that supports raw TCP connections:
```bash
ssh -p 2222 user@<comma3_ip_address>
```

## Features

### Terminal Emulation
- Full ANSI escape sequence support
- 256-color support
- Cursor positioning and movement
- Screen clearing and line clearing
- Text attributes (bold, underline)
- Proper terminal size reporting (COLUMNS, LINES environment variables)

### Display
- High-resolution rendering on Comma 3 screen (2160x1080)
- Monospace font rendering
- Real-time terminal updates
- Visual cursor indicator
- Touch-to-exit functionality (touch top area)

### Networking
- TCP socket-based communication
- Non-blocking I/O for responsive interaction
- Automatic client cleanup on disconnection
- Connection status display

## Architecture

The system uses a master/slave PTY architecture:

1. **PTY Master**: Controlled by the terminal emulator program
   - Reads output from the shell process
   - Writes input from user/network to the shell
   - Handles terminal control and escape sequences

2. **PTY Slave**: Connected to the shell process
   - Appears as a real terminal to the shell
   - Handles terminal I/O operations
   - Supports terminal control operations (resize, etc.)

3. **Shell Process**: Runs in the slave PTY
   - Standard bash shell with full functionality
   - Proper environment variables set
   - Supports all terminal applications (vim, htop, etc.)

## Advantages over tmux_viewer.cc

1. **Real Terminal**: Uses actual PTY instead of capturing tmux output
2. **Better Performance**: Direct PTY I/O instead of polling tmux commands
3. **Full Compatibility**: Works with all terminal applications
4. **Proper Input Handling**: Real keyboard input forwarding
5. **SSH Integration**: Native support for remote connections
6. **ANSI Support**: Proper escape sequence handling for colors and formatting
7. **Responsive**: Real-time updates without polling delays

## Security Notes

- The SSH PTY server listens on port 2222 (not standard SSH port 22)
- No authentication is implemented - this is for development/debugging only
- The server accepts one client connection at a time
- All traffic is unencrypted (raw TCP, not SSH protocol)

For production use, consider implementing proper SSH protocol support with authentication.

## Troubleshooting

### Connection Issues
- Ensure the Comma 3 device is accessible on the network
- Check that port 2222 is not blocked by firewall
- Verify the PTY server is running on the device

### Display Issues
- Touch the top area of the screen to exit
- Check that raylib and fonts are properly installed
- Verify screen resolution settings match device capabilities

### Terminal Issues
- Ensure TERM environment variable is set correctly
- Check that the shell process is running
- Verify PTY permissions and capabilities