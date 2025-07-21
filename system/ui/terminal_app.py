#!/usr/bin/env python3
import sys
import signal
import pyray as rl
from openpilot.system.ui.lib.application import GuiApplication
from openpilot.system.ui.widgets.terminal import TerminalWidget


class TerminalApp(GuiApplication):
    """Terminal application for Comma 3 display"""
    
    def __init__(self, session_id: str = "default"):
        super().__init__("Terminal", 2160, 1080)
        self.session_id = session_id
        self.terminal_widget: TerminalWidget = None
        self.running = True
        
        # Set up signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        print(f"Received signal {signum}, shutting down...")
        self.running = False
        if self.terminal_widget:
            self.terminal_widget.disconnect()
    
    def setup(self):
        """Set up the terminal application"""
        # Create terminal widget
        self.terminal_widget = TerminalWidget(self.session_id)
        
        # Set terminal widget to fill the entire screen
        terminal_rect = rl.Rectangle(0, 0, self.width, self.height)
        self.terminal_widget.set_rect(terminal_rect)
        
        print(f"Terminal app started for session: {self.session_id}")
    
    def update(self):
        """Update the terminal application"""
        if not self.running:
            return False
        
        # Check for exit key (ESC)
        if rl.is_key_pressed(rl.KEY_ESCAPE):
            self.running = False
            return False
        
        return True
    
    def render(self):
        """Render the terminal application"""
        rl.begin_drawing()
        rl.clear_background(rl.BLACK)
        
        if self.terminal_widget:
            self.terminal_widget.render()
        
        rl.end_drawing()
    
    def cleanup(self):
        """Clean up resources"""
        if self.terminal_widget:
            self.terminal_widget.disconnect()
        print("Terminal app cleaned up")


def main():
    """Main entry point for terminal application"""
    session_id = "default"
    
    # Parse command line arguments
    if len(sys.argv) > 1:
        session_id = sys.argv[1]
    
    print(f"Starting terminal app for session: {session_id}")
    
    # Create and run terminal application
    app = TerminalApp(session_id)
    
    try:
        app.run()
    except KeyboardInterrupt:
        print("Terminal app interrupted")
    finally:
        app.cleanup()


if __name__ == "__main__":
    main()