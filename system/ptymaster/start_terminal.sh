#!/bin/bash
# Quick start script for PTY Terminal System on Comma 3

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SESSION_ID="${1:-default}"

echo "Starting PTY Terminal System for Comma 3"
echo "Session ID: $SESSION_ID"
echo "Script directory: $SCRIPT_DIR"

# Check if we're on the Comma 3
if [ ! -d "/data/openpilot" ]; then
    echo "Warning: This doesn't appear to be a Comma 3 device"
    echo "Expected /data/openpilot directory not found"
fi

# Make sure we're in the right directory
cd "$SCRIPT_DIR/../.."

# Check Python dependencies
echo "Checking dependencies..."
python3 -c "import pyray" 2>/dev/null || {
    echo "Error: pyray not found. Please install with:"
    echo "  pip3 install pyray"
    exit 1
}

# Create SSH helper script
echo "Creating SSH helper script..."
python3 "$SCRIPT_DIR/launch_terminal.py" --create-ssh-script

# Start the terminal system
echo "Starting terminal system..."
echo "Press Ctrl+C to stop"
echo ""
echo "To connect via SSH, use:"
echo "  ssh user@comma3 '/tmp/ssh_terminal.sh $SESSION_ID'"
echo ""

exec python3 "$SCRIPT_DIR/launch_terminal.py" "$SESSION_ID"