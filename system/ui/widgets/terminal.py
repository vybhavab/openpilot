import re
import time
import threading
from collections import deque
from typing import List, Tuple, Optional
import pyray as rl
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.lib.application import gui_app, DEFAULT_TEXT_SIZE, FontWeight
from openpilot.system.ptymaster.session_manager import PTYClient


class TerminalBuffer:
    """Terminal buffer with VT100/ANSI escape sequence support"""
    
    def __init__(self, rows: int = 24, cols: int = 80):
        self.rows = rows
        self.cols = cols
        self.buffer: List[List[str]] = [[' ' for _ in range(cols)] for _ in range(rows)]
        self.colors: List[List[Tuple[int, int, int]]] = [[(255, 255, 255) for _ in range(cols)] for _ in range(rows)]
        self.cursor_row = 0
        self.cursor_col = 0
        self.scroll_top = 0
        self.scroll_bottom = rows - 1
        self.scrollback: deque = deque(maxlen=1000)
        self.scrollback_offset = 0
        
        # ANSI color map
        self.ansi_colors = {
            30: (0, 0, 0),        # Black
            31: (255, 0, 0),      # Red
            32: (0, 255, 0),      # Green
            33: (255, 255, 0),    # Yellow
            34: (0, 0, 255),      # Blue
            35: (255, 0, 255),    # Magenta
            36: (0, 255, 255),    # Cyan
            37: (255, 255, 255),  # White
            90: (128, 128, 128),  # Bright Black
            91: (255, 128, 128),  # Bright Red
            92: (128, 255, 128),  # Bright Green
            93: (255, 255, 128),  # Bright Yellow
            94: (128, 128, 255),  # Bright Blue
            95: (255, 128, 255),  # Bright Magenta
            96: (128, 255, 255),  # Bright Cyan
            97: (255, 255, 255),  # Bright White
        }
        self.current_color = (255, 255, 255)
        
        # Escape sequence regex
        self.escape_regex = re.compile(r'\x1b\[[0-9;]*[a-zA-Z]')
    
    def resize(self, rows: int, cols: int):
        """Resize the terminal buffer"""
        if rows == self.rows and cols == self.cols:
            return
        
        old_buffer = self.buffer
        old_colors = self.colors
        
        self.rows = rows
        self.cols = cols
        self.buffer = [[' ' for _ in range(cols)] for _ in range(rows)]
        self.colors = [[(255, 255, 255) for _ in range(cols)] for _ in range(rows)]
        
        # Copy old content
        for i in range(min(len(old_buffer), rows)):
            for j in range(min(len(old_buffer[i]), cols)):
                self.buffer[i][j] = old_buffer[i][j]
                self.colors[i][j] = old_colors[i][j]
        
        self.cursor_row = min(self.cursor_row, rows - 1)
        self.cursor_col = min(self.cursor_col, cols - 1)
        self.scroll_bottom = rows - 1
    
    def write(self, data: str):
        """Write data to terminal buffer with ANSI escape sequence processing"""
        # Remove escape sequences and process them
        pos = 0
        while pos < len(data):
            match = self.escape_regex.search(data, pos)
            if match:
                # Write text before escape sequence
                if match.start() > pos:
                    self._write_text(data[pos:match.start()])
                
                # Process escape sequence
                self._process_escape_sequence(match.group())
                pos = match.end()
            else:
                # Write remaining text
                self._write_text(data[pos:])
                break
    
    def _write_text(self, text: str):
        """Write plain text to buffer"""
        for char in text:
            if char == '\n':
                self._newline()
            elif char == '\r':
                self.cursor_col = 0
            elif char == '\b':
                if self.cursor_col > 0:
                    self.cursor_col -= 1
            elif char == '\t':
                # Tab to next 8-character boundary
                self.cursor_col = ((self.cursor_col // 8) + 1) * 8
                if self.cursor_col >= self.cols:
                    self._newline()
            elif ord(char) >= 32:  # Printable character
                if self.cursor_col >= self.cols:
                    self._newline()
                
                self.buffer[self.cursor_row][self.cursor_col] = char
                self.colors[self.cursor_row][self.cursor_col] = self.current_color
                self.cursor_col += 1
    
    def _newline(self):
        """Move to next line, scrolling if necessary"""
        self.cursor_col = 0
        if self.cursor_row >= self.scroll_bottom:
            self._scroll_up()
        else:
            self.cursor_row += 1
    
    def _scroll_up(self):
        """Scroll the terminal up by one line"""
        # Save top line to scrollback
        self.scrollback.append((
            self.buffer[self.scroll_top][:],
            self.colors[self.scroll_top][:]
        ))
        
        # Shift lines up
        for i in range(self.scroll_top, self.scroll_bottom):
            self.buffer[i] = self.buffer[i + 1][:]
            self.colors[i] = self.colors[i + 1][:]
        
        # Clear bottom line
        self.buffer[self.scroll_bottom] = [' '] * self.cols
        self.colors[self.scroll_bottom] = [(255, 255, 255)] * self.cols
    
    def _process_escape_sequence(self, seq: str):
        """Process ANSI escape sequences"""
        if seq.startswith('\x1b['):
            params = seq[2:-1]
            command = seq[-1]
            
            if command == 'm':  # Color/style
                self._process_color_sequence(params)
            elif command == 'H' or command == 'f':  # Cursor position
                self._process_cursor_position(params)
            elif command == 'A':  # Cursor up
                count = int(params) if params else 1
                self.cursor_row = max(0, self.cursor_row - count)
            elif command == 'B':  # Cursor down
                count = int(params) if params else 1
                self.cursor_row = min(self.rows - 1, self.cursor_row + count)
            elif command == 'C':  # Cursor right
                count = int(params) if params else 1
                self.cursor_col = min(self.cols - 1, self.cursor_col + count)
            elif command == 'D':  # Cursor left
                count = int(params) if params else 1
                self.cursor_col = max(0, self.cursor_col - count)
            elif command == 'J':  # Clear screen
                self._clear_screen(params)
            elif command == 'K':  # Clear line
                self._clear_line(params)
    
    def _process_color_sequence(self, params: str):
        """Process color escape sequences"""
        if not params:
            params = "0"
        
        for param in params.split(';'):
            if param:
                code = int(param)
                if code == 0:  # Reset
                    self.current_color = (255, 255, 255)
                elif code in self.ansi_colors:
                    self.current_color = self.ansi_colors[code]
    
    def _process_cursor_position(self, params: str):
        """Process cursor position escape sequences"""
        if not params:
            self.cursor_row = 0
            self.cursor_col = 0
        else:
            parts = params.split(';')
            if len(parts) >= 2:
                self.cursor_row = max(0, min(self.rows - 1, int(parts[0]) - 1))
                self.cursor_col = max(0, min(self.cols - 1, int(parts[1]) - 1))
    
    def _clear_screen(self, params: str):
        """Clear screen based on parameter"""
        param = int(params) if params else 0
        
        if param == 0:  # Clear from cursor to end of screen
            for i in range(self.cursor_row, self.rows):
                start_col = self.cursor_col if i == self.cursor_row else 0
                for j in range(start_col, self.cols):
                    self.buffer[i][j] = ' '
                    self.colors[i][j] = (255, 255, 255)
        elif param == 1:  # Clear from beginning of screen to cursor
            for i in range(0, self.cursor_row + 1):
                end_col = self.cursor_col if i == self.cursor_row else self.cols - 1
                for j in range(0, end_col + 1):
                    self.buffer[i][j] = ' '
                    self.colors[i][j] = (255, 255, 255)
        elif param == 2:  # Clear entire screen
            for i in range(self.rows):
                for j in range(self.cols):
                    self.buffer[i][j] = ' '
                    self.colors[i][j] = (255, 255, 255)
    
    def _clear_line(self, params: str):
        """Clear line based on parameter"""
        param = int(params) if params else 0
        
        if param == 0:  # Clear from cursor to end of line
            for j in range(self.cursor_col, self.cols):
                self.buffer[self.cursor_row][j] = ' '
                self.colors[self.cursor_row][j] = (255, 255, 255)
        elif param == 1:  # Clear from beginning of line to cursor
            for j in range(0, self.cursor_col + 1):
                self.buffer[self.cursor_row][j] = ' '
                self.colors[self.cursor_row][j] = (255, 255, 255)
        elif param == 2:  # Clear entire line
            for j in range(self.cols):
                self.buffer[self.cursor_row][j] = ' '
                self.colors[self.cursor_row][j] = (255, 255, 255)
    
    def get_display_lines(self) -> List[Tuple[str, List[Tuple[int, int, int]]]]:
        """Get lines for display, including scrollback if offset is set"""
        lines = []
        
        if self.scrollback_offset > 0:
            # Show scrollback content
            scrollback_list = list(self.scrollback)
            start_idx = max(0, len(scrollback_list) - self.scrollback_offset)
            
            for i in range(start_idx, len(scrollback_list)):
                line_chars, line_colors = scrollback_list[i]
                line_text = ''.join(line_chars)
                lines.append((line_text, line_colors))
            
            # Fill remaining lines with current buffer
            remaining = self.rows - len(lines)
            for i in range(remaining):
                if i < len(self.buffer):
                    line_text = ''.join(self.buffer[i])
                    lines.append((line_text, self.colors[i]))
        else:
            # Show current buffer
            for i in range(self.rows):
                line_text = ''.join(self.buffer[i])
                lines.append((line_text, self.colors[i]))
        
        return lines


class TerminalWidget(Widget):
    """Terminal emulator widget for openpilot UI"""
    
    def __init__(self, session_id: str = "default"):
        super().__init__()
        self.session_id = session_id
        self.terminal_buffer = TerminalBuffer()
        self.pty_client: Optional[PTYClient] = None
        self.font_size = 24
        self.line_height = 28
        self.char_width = 14
        self.connected = False
        self.last_cursor_blink = time.time()
        self.cursor_visible = True
        self.background_color = rl.Color(0, 0, 0, 255)
        self.cursor_color = rl.Color(255, 255, 255, 255)
        
        # Input handling
        self.input_buffer = ""
        self.shift_pressed = False
        
        # Connect to PTY service
        self._connect_to_pty()
    
    def _connect_to_pty(self):
        """Connect to PTY service"""
        try:
            self.pty_client = PTYClient()
            self.pty_client.set_data_callback(self._on_pty_data)
            
            if self.pty_client.connect(self.session_id):
                self.connected = True
                # Update terminal size
                self._update_terminal_size()
            else:
                self.connected = False
        except Exception as e:
            print(f"Failed to connect to PTY service: {e}")
            self.connected = False
    
    def _on_pty_data(self, data: bytes):
        """Handle data received from PTY"""
        try:
            text = data.decode('utf-8', errors='ignore')
            self.terminal_buffer.write(text)
        except Exception as e:
            print(f"Error processing PTY data: {e}")
    
    def _update_terminal_size(self):
        """Update terminal size based on widget dimensions"""
        if not self.connected or not self.pty_client:
            return
        
        cols = max(1, int(self._rect.width // self.char_width))
        rows = max(1, int(self._rect.height // self.line_height))
        
        if cols != self.terminal_buffer.cols or rows != self.terminal_buffer.rows:
            self.terminal_buffer.resize(rows, cols)
            self.pty_client.resize_terminal(rows, cols)
    
    def _update_layout_rects(self):
        """Update layout when widget rect changes"""
        super()._update_layout_rects()
        self._update_terminal_size()
    
    def _handle_keyboard_input(self):
        """Handle keyboard input"""
        # Handle key presses
        key = rl.get_key_pressed()
        while key != 0:
            self._process_key(key)
            key = rl.get_key_pressed()
        
        # Handle character input
        char = rl.get_char_pressed()
        while char != 0:
            if 32 <= char <= 126:  # Printable ASCII
                self._send_char(chr(char))
            char = rl.get_char_pressed()
    
    def _process_key(self, key: int):
        """Process special keys"""
        if not self.connected or not self.pty_client:
            return
        
        key_map = {
            rl.KEY_ENTER: '\r',
            rl.KEY_TAB: '\t',
            rl.KEY_BACKSPACE: '\b',
            rl.KEY_DELETE: '\x7f',
            rl.KEY_UP: '\x1b[A',
            rl.KEY_DOWN: '\x1b[B',
            rl.KEY_RIGHT: '\x1b[C',
            rl.KEY_LEFT: '\x1b[D',
            rl.KEY_HOME: '\x1b[H',
            rl.KEY_END: '\x1b[F',
            rl.KEY_PAGE_UP: '\x1b[5~',
            rl.KEY_PAGE_DOWN: '\x1b[6~',
        }
        
        if key in key_map:
            self.pty_client.send_input(key_map[key])
        elif key == rl.KEY_LEFT_SHIFT or key == rl.KEY_RIGHT_SHIFT:
            self.shift_pressed = True
    
    def _send_char(self, char: str):
        """Send character to PTY"""
        if self.connected and self.pty_client:
            self.pty_client.send_input(char)
    
    def _render(self, rect: rl.Rectangle) -> None:
        """Render the terminal widget"""
        # Handle keyboard input
        self._handle_keyboard_input()
        
        # Draw background
        rl.draw_rectangle_rec(rect, self.background_color)
        
        if not self.connected:
            # Show connection status
            text = "Connecting to PTY service..."
            text_width = rl.measure_text(text, self.font_size)
            x = rect.x + (rect.width - text_width) // 2
            y = rect.y + rect.height // 2
            rl.draw_text(text, int(x), int(y), self.font_size, rl.WHITE)
            return
        
        # Get display lines from terminal buffer
        lines = self.terminal_buffer.get_display_lines()
        
        # Render each line
        y_offset = rect.y
        for i, (line_text, line_colors) in enumerate(lines):
            if y_offset + self.line_height > rect.y + rect.height:
                break
            
            x_offset = rect.x
            
            # Render each character with its color
            for j, char in enumerate(line_text):
                if x_offset + self.char_width > rect.x + rect.width:
                    break
                
                if j < len(line_colors):
                    r, g, b = line_colors[j]
                    color = rl.Color(r, g, b, 255)
                else:
                    color = rl.WHITE
                
                if char != ' ':
                    rl.draw_text(char, int(x_offset), int(y_offset), self.font_size, color)
                
                # Draw cursor
                if (i == self.terminal_buffer.cursor_row and 
                    j == self.terminal_buffer.cursor_col and
                    self.cursor_visible):
                    cursor_rect = rl.Rectangle(x_offset, y_offset, self.char_width, self.line_height)
                    rl.draw_rectangle_rec(cursor_rect, self.cursor_color)
                
                x_offset += self.char_width
            
            y_offset += self.line_height
        
        # Blink cursor
        current_time = time.time()
        if current_time - self.last_cursor_blink > 0.5:
            self.cursor_visible = not self.cursor_visible
            self.last_cursor_blink = current_time
    
    def disconnect(self):
        """Disconnect from PTY service"""
        if self.pty_client:
            self.pty_client.disconnect()
            self.pty_client = None
        self.connected = False